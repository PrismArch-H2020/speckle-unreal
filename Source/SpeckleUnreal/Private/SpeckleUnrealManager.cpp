#include "SpeckleUnrealManager.h"
#include "HttpManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Kismet/GameplayStatics.h"

#include "Objects/RenderMaterial.h"


// Sets default values
ASpeckleUnrealManager::ASpeckleUnrealManager()
{
	static ConstructorHelpers::FObjectFinder<UMaterial> SpeckleMaterial(TEXT("Material'/SpeckleUnreal/SpeckleMaterial.SpeckleMaterial'"));
	static ConstructorHelpers::FObjectFinder<UMaterial> SpeckleGlassMaterial(TEXT("Material'/SpeckleUnreal/SpeckleGlassMaterial.SpeckleGlassMaterial'"));

	//When the object is constructed, Get the HTTP module
	Http = &FHttpModule::Get();
	
	SetRootComponent(CreateDefaultSubobject<USceneComponent>("Root"));
	RootComponent->SetRelativeScale3D(FVector(-1,1,1));
    RootComponent->SetMobility(EComponentMobility::Movable); 
	
	DefaultMeshMaterial = SpeckleMaterial.Object;
	BaseMeshOpaqueMaterial = SpeckleMaterial.Object;
	BaseMeshTransparentMaterial = SpeckleGlassMaterial.Object;
}

// Called when the game starts or when spawned
void ASpeckleUnrealManager::BeginPlay()
{
	Super::BeginPlay();
	if (ImportAtRuntime)
		ImportSpeckleObject();
}

/*Import the Speckle object*/
void ASpeckleUnrealManager::ImportSpeckleObject()
{
	const FString UserAgent = FString::Printf(TEXT("Unreal Engine (%s) / %d.%d.%d"),
		*UGameplayStatics::GetPlatformName(), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION);

#if !SUPPRESS_SPECKLE_ANALYTICS
	const FString HostApplication = FString::Printf(TEXT("Unreal%%20Engine%%20%d"), ENGINE_MAJOR_VERSION);
	const FString Action = "receive/manual";
	FString SpeckleUserID = "No%20SUUID";
	
#if PLATFORM_WINDOWS
	const FString UserPath = UKismetSystemLibrary::GetPlatformUserDir().LeftChop(10); //remove "Documents/"
	const FString Dir = FString::Printf(TEXT("%sAppData/Roaming/Speckle/suuid"), *UserPath);
	FFileHelper::LoadFileToString(SpeckleUserID, *Dir);
#endif	
	//TODO MACOS
	
	//Track page view
	const FString ViewURL = FString::Printf(
		TEXT("https://speckle.matomo.cloud/matomo.php?idsite=2&rec=1&apiv=1&uid=%s&action_name=%s&url=http://connectors/%s/%s&urlref=http://connectors/%s/%s&_cvar=%%7B%%22hostApplication%%22:%%20%%22%s%%22%%7D"),
		*SpeckleUserID,
		*Action,
		*HostApplication,
		*Action,
		*HostApplication,
		*Action,
		*HostApplication
	);

	const FHttpRequestRef ViewTrackingRequest = Http->CreateRequest();
	ViewTrackingRequest->SetVerb("POST");
	ViewTrackingRequest->SetURL(ViewURL);
	ViewTrackingRequest->SetHeader("User-Agent", UserAgent);
	ViewTrackingRequest->ProcessRequest();
	
	//Track receive action
	const FString EventURL = FString::Printf(
		TEXT("https://speckle.matomo.cloud/matomo.php?idsite=2&rec=1&apiv=1&uid=%s&_cvar=%%7B%%22hostApplication%%22:%%20%%22%s%%22%%7D&e_c=%s&e_a=%s"),
			*SpeckleUserID,
			*HostApplication,
			*HostApplication,
			*Action
		);
	
	const FHttpRequestRef EventTrackingRequest = Http->CreateRequest();
	EventTrackingRequest->SetVerb("POST");
	EventTrackingRequest->SetURL(EventURL);
	EventTrackingRequest->SetHeader("User-Agent", UserAgent);
	
	EventTrackingRequest->ProcessRequest();
	
#endif

	
	const FString url = ServerUrl + "/objects/" + StreamID + "/" + ObjectID;
	//GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green, "[Speckle] Downloading: " + url);
	
	const FHttpRequestRef Request = Http->CreateRequest();
	
	Request->SetVerb("GET");
	Request->SetHeader("Accept", TEXT("text/plain"));
	Request->SetHeader("Authorization", "Bearer " + AuthToken);
	Request->OnProcessRequestComplete().BindUObject(this, &ASpeckleUnrealManager::OnStreamTextResponseReceived);
	Request->SetURL(url);
	Request->SetHeader("User-Agent", UserAgent);
	Request->ProcessRequest();
}

void ASpeckleUnrealManager::OnStreamTextResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, "Stream Request failed: " + Response->GetContentAsString());
		return;
	}
	auto responseCode = Response->GetResponseCode();
	if (responseCode != 200)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, FString::Printf(TEXT("Error response. Response code %d"), responseCode));
		return;
	}

	FString response = Response->GetContentAsString();
	
	// ParseIntoArray is very inneficient for large strings.
	// https://docs.unrealengine.com/en-US/API/Runtime/Core/Containers/FString/ParseIntoArrayLines/index.html
	// https://answers.unrealengine.com/questions/81697/reading-text-file-line-by-line.html
	// Can be fixed by setting the size of the array
	int lineCount = 0;
	for (const TCHAR* ptr = *response; *ptr; ptr++)
		if (*ptr == '\n')
			lineCount++;
	
	TArray<FString> lines;
	lines.Reserve(lineCount);
	response.ParseIntoArray(lines, TEXT("\n"), true);

	GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green, FString::Printf(TEXT("[Speckle] Parsing %d downloaded objects..."), lineCount));


	for (const FString& line : lines)
	{
		FString objectId, objectJson;
		if (!line.Split("\t", &objectId, &objectJson))
			continue;
		
		TSharedPtr<FJsonObject> jsonObject;
		TSharedRef<TJsonReader<>> jsonReader = TJsonReaderFactory<>::Create(objectJson);
		if (!FJsonSerializer::Deserialize(jsonReader, jsonObject))
			continue;

		SpeckleObjects.Add(objectId, jsonObject);
	}

	GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green, FString::Printf(TEXT("[Speckle] Converting %d objects..."), lineCount));

	//World Units setup
	WorldToCentimeters = 1; //Default value of 1uu = 1cm

	AWorldSettings* WorldSettings;
	if(IsValid(World = GetWorld() ) && IsValid(WorldSettings = World->GetWorldSettings()) )
	{
		WorldToCentimeters = WorldSettings->WorldToMeters / 100;
	}

	//ImportObjectFromCache(this, SpeckleObjects[ObjectID]);

	// Provide a Meshes to Layers map

	ObjectsMap.Empty();
	ObjectsMap = ImportObjectFromCacheNew(this, SpeckleObjects[ObjectID], NULL,
													ObjectsMap, "STARTER");
	
	UE_LOG(LogTemp, Warning, TEXT("--------------------------------"));
	UE_LOG(LogTemp, Warning, TEXT("MAP Layers Object Hashes : %d"), ObjectsMap.Num());
	UE_LOG(LogTemp, Warning, TEXT("--------------------------------"));
	
	for (auto& Elem : ObjectsMap)
	{
		UE_LOG(LogTemp, Warning, TEXT("KEY VAL: %s - %s"), *Elem.Key, *Elem.Value);
	}
	
	for (const auto& m : CreatedObjectsCache)
	{
		if(AActor* a = Cast<AActor>(m))
			a->Destroy();
		else
			m->ConditionalBeginDestroy();
	}

	CreatedObjectsCache = InProgressObjectsCache;
	InProgressObjectsCache.Empty();
	
	GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green,
		FString::Printf(TEXT("[Speckle] Objects imported successfully. Created %d Actors"),
											CreatedObjectsCache.Num()));

	OnCommitJsonParsedDynamic.Broadcast("Completed");
}


void ASpeckleUnrealManager::DeleteObjects()
{
	ConvertedMaterials.Empty();
	
	for (const auto& m : CreatedObjectsCache)
	{
		if(AActor* a = Cast<AActor>(m))
			a->Destroy();
		else
			m->ConditionalBeginDestroy();
		
	}

	CreatedObjectsCache.Empty();
}

// Parse JSON : Streams list
void ASpeckleUnrealManager::OnStreamItemsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, "Stream Request failed: " + Response->GetContentAsString());
		return;
	}

	auto responseCode = Response-> GetResponseCode();
	if (responseCode != 200)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red,
        FString::Printf(TEXT("SpeckleUnrealManager 4: Error response. Response code %d"), responseCode));
		return;
	}

	FString response = Response->GetContentAsString();
	//Create a pointer to hold the json serialized data
	TSharedPtr<FJsonObject> JsonObject;
	//Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(response);

	ArrayOfStreams.Empty();
	//Deserialize the json data given Reader and the actual object to deserialize
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		for(const auto& pair:JsonObject->Values)
		{
			// nested objects
			auto StreamsArr = JsonObject->GetObjectField(TEXT("data"))
														            ->GetObjectField(TEXT("user"))
																	   ->GetObjectField(TEXT("streams"))
																			->GetArrayField(TEXT("items"));

			// FString OutputString;
			// TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);
			// FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
			
			for (auto s : StreamsArr)
			{
				auto ID = s->AsObject()->GetStringField("id");
				auto Name = s->AsObject()->GetStringField("name");
				auto Description = s->AsObject()->GetStringField("description");
				auto UpdatedAt = s->AsObject()->GetStringField("updatedAt");
				auto CreatedAt = s->AsObject()->GetStringField("createdAt");
				auto RoleUser = s->AsObject()->GetStringField("role");
				auto IsPublic = s->AsObject()->GetBoolField("isPublic");

				//GEngine->AddOnScreenDebugMessage(-1, 25.f, FColor::White, RoleUser);
				auto Stream = FSpeckleStream(ID, Name, Description, IsPublic, RoleUser, CreatedAt, UpdatedAt);
				
				ArrayOfStreams.Add(Stream);
			}
		}
	}

	OnStreamsProcessedDynamic.Broadcast(ArrayOfStreams);
}

// Parse JSON of Branches
void ASpeckleUnrealManager::OnBranchesItemsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response,
																									bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, "Stream Request failed: " +
																						Response->GetContentAsString());
		return;
	}

	auto responseCode = Response-> GetResponseCode();

	FString MsgBody = Response->GetContentAsString();
	//UE_LOG(LogTemp, Warning, TEXT("BRANCHES RESPONSE: %s"), *MsgBody);
	
	if (responseCode != 200)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red,
		FString::Printf(TEXT("SpeckleUnrealManager 3: Error response. Response code %d"), responseCode));
		return;
	}

	FString response = Response->GetContentAsString();
	//Create a pointer to hold the json serialized data
	TSharedPtr<FJsonObject> JsonObject;
	//Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(response);

	ArrayOfBranches.Empty();
	
	//Deserialize the json data given Reader and the actual object to deserialize
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		for(const auto& pair:JsonObject->Values)
		{
			auto BranchesArr = JsonObject->GetObjectField(TEXT("data"))
			->GetObjectField(TEXT("stream"))
			->GetObjectField(TEXT("branches"))
			->GetArrayField(TEXT("items"));

			for (auto b : BranchesArr)
			{
				auto ID = b->AsObject()->GetStringField("id");
				auto Name = b->AsObject()->GetStringField("name");
				auto Description = b->AsObject()->GetStringField("description");
				auto Branch = FSpeckleBranch(ID, Name, Description);
				ArrayOfBranches.Add(Branch);
			}
		}
	}

	OnBranchesProcessedDynamic.Broadcast(ArrayOfBranches);
	OnBranchesProcessed.Broadcast(ArrayOfBranches);
}

// Parse Json : List of Commits
void ASpeckleUnrealManager::OnCommitsItemsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response,
																									bool bWasSuccessful)
{	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, "Stream Request failed: " +
																						Response->GetContentAsString());
		return;
	}

	auto responseCode = Response-> GetResponseCode();
	if (responseCode != 200)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red,
		FString::Printf(TEXT("SpeckleUnrealManager 2: Error response. Response code %d"), responseCode));
		return;
	}

	FString response = Response->GetContentAsString();
	//Create a pointer to hold the json serialized data
	TSharedPtr<FJsonObject> JsonObject;
	//Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(response);

	// try
	// {
		ArrayOfCommits.Empty();
	// } catch (...)
	// {
	// 	GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, "Could not empty array of commits");
	// }
	
	//Deserialize the json data given Reader and the actual object to deserialize
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		for(const auto& pair:JsonObject->Values)
		{
			// Nested Json objects
			auto CommitsArr = JsonObject->GetObjectField(TEXT("data"))
																	->GetObjectField(TEXT("stream"))
																		->GetObjectField(TEXT("branch"))
																			->GetObjectField(TEXT("commits"))
																				->GetArrayField(TEXT("items"));
			for (auto commit : CommitsArr)
			{
				// --- Write Json to log output (for debugging) ---
				// FString OutputString;
				// TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);
				// FJsonSerializer::Serialize(commit->AsObject().ToSharedRef(), Writer);
				// UE_LOG(LogTemp, Warning, TEXT("resulting jsonString -> %s"), *OutputString);
				//  -- //

				TSharedPtr<FJsonObject> commitAsObject = commit->AsObject();
				
				FString ObjID, Message, AuthorName, BranchName, Id, SourceApplication, TotalChildrenCount, Parents,
					AuthorId, AuthorAvatar, CreatedAt;
				
				commitAsObject->TryGetStringField("referencedObject", ObjID);
				commitAsObject->TryGetStringField("message", Message);
				commitAsObject->TryGetStringField("authorName", AuthorName);
				commitAsObject->TryGetStringField("branchName", BranchName);
				commitAsObject->TryGetStringField("id", Id);
				commitAsObject->TryGetStringField("sourceApplication", SourceApplication);
				commitAsObject->TryGetStringField("totalChildrenCount", TotalChildrenCount);
				commitAsObject->TryGetStringField("parents", Parents);
				commitAsObject->TryGetStringField("authorId", AuthorId);
				commitAsObject->TryGetStringField("authorAvatar", AuthorAvatar);
				commitAsObject->TryGetStringField("createdAt", CreatedAt);

				//auto Commit = FSpeckleCommit(ObjID, AuthorName, Message, BranchName);
				FSpeckleCommit Commit = FSpeckleCommit(ObjID, AuthorName, Message, BranchName,
						Id, SourceApplication, TotalChildrenCount, Parents,
						AuthorId, AuthorAvatar, CreatedAt);
				
				ArrayOfCommits.Add(Commit);
			}
		}
	}
	
	OnCommitsProcessedDynamic.Broadcast(ArrayOfCommits);
	OnCommitsProcessed.Broadcast(ArrayOfCommits);
}

// Send Query for List of Streams
void ASpeckleUnrealManager::FetchStreamItems(FString PostPayload,
											   TFunction<void(FHttpRequestPtr, FHttpResponsePtr , bool)> HandleResponse)
{
	FString url = ServerUrl + "/graphql";

	FHttpRequestRef Request = Http->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader("Accept-Encoding", TEXT("gzip")); 
	Request->SetHeader("Content-Type", TEXT("application/json"));
	Request->SetHeader("Authorization", "Bearer " + AuthToken);

	Request->SetContentAsString(PostPayload);
	
	Request->OnProcessRequestComplete().BindLambda([=](
		FHttpRequestPtr request,
		FHttpResponsePtr response,
		bool success)
		{ HandleResponse(request, response, success); });

	Request->SetURL(url);
	Request->ProcessRequest();
}


//=====================
void ASpeckleUnrealManager::FetchGlobalVariables(const FString& ServerName, const FString& Stream, const FString& Bearer)
{
	FString url = ServerName + "/graphql";
	auto HttpMod = &FHttpModule::Get();
	FHttpRequestRef Request = HttpMod->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader("Accept-Encoding", TEXT("gzip"));
	Request->SetHeader("Content-Type", TEXT("application/json"));
	Request->SetHeader("Accept", TEXT("application/json"));
	Request->SetHeader("DNT", TEXT("1"));
	Request->SetHeader("Origin", TEXT("https://speckle.xyz"));
	Request->SetHeader("Authorization", "Bearer " + AuthToken);

	FString PostPayload = "{\"query\": \"query{stream (id: \\\"" + Stream + "\\\"){branch(name:  \\\"" + "globals" +
															"\\\"){commits{totalCount items{referencedObject}}}}}\"}";
	Request->SetContentAsString(PostPayload);
	Request->OnProcessRequestComplete().BindLambda([=]
    (FHttpRequestPtr request, FHttpResponsePtr Response, bool success)
    {
        auto responseCode = Response-> GetResponseCode();

        FString response = Response->GetContentAsString();

		UE_LOG(LogTemp, Warning, TEXT("Response for Stream  ----- : %s"), *Stream);
		UE_LOG(LogTemp, Warning, TEXT("Response of custom json Globals ----- : %s"), *response);

		//https://www.orfeasel.com/parsing-json-files/
		//GLog->Log("Json String:");
		//GLog->Log(response);
		
        //Create a pointer to hold the json serialized data
		//TSharedPtr<FJsonObject> JsonObject;
        TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject()); 

		//Create a reader pointer to read the json data
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(response);
		
        auto RefObjectID = FString();
	
        //Deserialize the json data given Reader and the actual object to deserialize
		//if (FJsonSerializer::Deserialize(Reader, JsonObject))
        if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
        {
	        for(const auto& pair:JsonObject->Values)
	        {
				TSharedPtr<FJsonObject> JsonData = JsonObject->GetObjectField(TEXT("data"))
																	->GetObjectField(TEXT("stream"));

				//output json object to ue4 output log for debugging purposes - https://stackoverflow.com/a/50376462
				/*FString OutputString;
				TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);
				FJsonSerializer::Serialize(JsonData.ToSharedRef(), Writer);
				UE_LOG(LogTemp, Warning, TEXT("resulting jsonString -> %s"), *OutputString); */
				
				//possibly always true - consider removing the if/else part
				if(!JsonData->HasField(TEXT("branch")))
	        	{
					UE_LOG(LogTemp, Warning, TEXT("Stream: %s does not have branch field ----------------"), *Stream);
					//continue;
					return;
	        	} else
	        	{
					//UE_LOG(LogTemp, Warning, TEXT(" Entering else path --------------------------"));

					TSharedPtr< FJsonValue > BranchValue = JsonData->TryGetField(TEXT("branch"));
					if (BranchValue.IsValid() && !(BranchValue->IsNull()) )
					{
						JsonData = BranchValue->AsObject();
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("Stream: %s has null branch field -----------"), *Stream);
					}
	        	}
       	        	
			
                const TSharedPtr<FJsonObject>* Commits;
				//BranchValue->AsObject()
	        	if(!JsonData->TryGetObjectField("commits", Commits))
	        	{				
	        		return;
	        	}
	        	
				RefObjectID = Commits->Get()->GetArrayField("items")[0]->AsObject()->GetStringField("referencedObject");
                //GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, FString::Printf(TEXT("Ref: %s"), *RefObjectID));
	        	
                TFunction<void(FHttpRequestPtr, FHttpResponsePtr , bool)> TempResponseHandler =
                				[=](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
                {
                					
                    FString responseStr = Response->GetContentAsString();
                    TSharedPtr<FJsonObject> JsonObject;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(responseStr);
                					
                    if (FJsonSerializer::Deserialize(Reader, JsonObject))
                    {
                         for(const auto& pair:JsonObject->Values)
                         {
         	
                           auto GlobalObject = JsonObject->GetObjectField(TEXT("data"))
															                  ->GetObjectField(TEXT("stream"))
																			   ->GetObjectField(TEXT("object"))
																                    ->GetObjectField(TEXT("data"));

						   /*GLog->Log("Globals json String:");
						   GLog->Log(responseStr);
						   FString OutputString;
						   TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);
						   FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
						   UE_LOG(LogTemp, Warning, TEXT("resulting jsonString -> %s"), *OutputString); */

                           FString RegionOut = FString("Empty");
                           GlobalObject->TryGetStringField("Region", RegionOut);
                         	
                           double LatitudeOut = 0.0f;
                           GlobalObject->TryGetNumberField("Latitude", LatitudeOut);

                           double LongitudeOut = 0.0f;
                           GlobalObject->TryGetNumberField("Longitude", LongitudeOut);

                           double HeightOut = 0.0f;
						   GlobalObject->TryGetNumberField("Height", HeightOut);

                         	
                         	
                         	FSpeckleGlobals Global = FSpeckleGlobals(RefObjectID, RegionOut,
                         											static_cast<float>(LatitudeOut),
                         											static_cast<float>(LongitudeOut),
                         											static_cast<float>(HeightOut)
                         											);

                         	// try
                         	// {
                         		OnGlobalsProcessedDynamic.Broadcast(Global, Stream);
                         	// }catch (...)
                         	// {
                         	// 	GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red,
                         	// 		"Sorry could not broadcast Globals for parsing");
                         	// }
                         }
                    }
                };

                FString PostPayload = "{\"query\": \"query{stream (id:\\\"" + Stream +
                	"\\\"){id name description updatedAt createdAt role isPublic object(id:\\\"" + RefObjectID +
                		"\\\"){id data}}}\"}";
	        	
                FetchStreamItems(PostPayload, TempResponseHandler);
            }
        }
    });
	
	Request->SetURL(url);
	Request->ProcessRequest();
}

// For Custom GraphQL fetch Json
void ASpeckleUnrealManager::FetchJson(const FString& CustomBearer, const FString& GraphQLPayload,
								TFunction<void(FHttpRequestPtr, FHttpResponsePtr , bool)> HandleResponse)
{
	FString url = this->ServerUrl + "/graphql";
	auto HttpMod = &FHttpModule::Get();
	FHttpRequestRef Request = Http->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader("Accept-Encoding", TEXT("gzip"));
	Request->SetHeader("Content-Type", TEXT("application/json"));
	Request->SetHeader("Accept", TEXT("application/json"));
	Request->SetHeader("DNT", TEXT("1"));
	Request->SetHeader("Origin", TEXT("https://speckle.xyz"));
	Request->SetHeader("Authorization", "Bearer " + CustomBearer);

	UE_LOG(LogTemp, Warning, TEXT("GraphQLPayload Final: %s"), *GraphQLPayload);
	
	Request->SetContentAsString(GraphQLPayload);
	
	Request->OnProcessRequestComplete().BindLambda([=](
		FHttpRequestPtr request,
		FHttpResponsePtr response,
		bool success)
		{ HandleResponse(request, response, success); });


	UE_LOG(LogTemp, Warning, TEXT("url: %s"), *url);
	
	Request->SetURL(url);
	Request->ProcessRequest();
}

// Parse JSON: Custom GraphQL
void ASpeckleUnrealManager::OnGraphQLJsonReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red, "Stream Request failed: " +
																						Response->GetContentAsString());
		return;
	}

	auto responseCode = Response-> GetResponseCode();
	if (responseCode != 200)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.0f, FColor::Red,
		FString::Printf(TEXT("SpeckleUnrealManager 2: Error response. Response code %d"), responseCode));
		return;
	}

	FString response = Response->GetContentAsString();

	UE_LOG(LogTemp, Warning, TEXT("Response of custom json : %s"), *response);
    OnGraphQLProcessedDynamic.Broadcast(response);
}