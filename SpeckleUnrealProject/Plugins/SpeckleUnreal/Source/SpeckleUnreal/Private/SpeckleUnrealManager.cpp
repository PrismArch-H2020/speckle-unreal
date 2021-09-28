#include "SpeckleUnrealManager.h"

// Sets default values
ASpeckleUnrealManager::ASpeckleUnrealManager()
{
	//When the object is constructed, Get the HTTP module
	Http = &FHttpModule::Get();
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
	FString url = ServerUrl + "/objects/" + StreamID + "/" + ObjectID;
	GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green, "[Speckle] Downloading: " + url);

	FHttpRequestRef Request = Http->CreateRequest();
	
	Request->SetVerb("GET");
	Request->SetHeader("Accept", TEXT("text/plain"));
	Request->SetHeader("Authorization", "Bearer " + AuthToken);

	Request->OnProcessRequestComplete().BindUObject(this, &ASpeckleUnrealManager::OnStreamTextResponseReceived);
	Request->SetURL(url);
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


	for (const auto& line : lines)
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

	if(Converters.Num() <= 0)
	{
		const FString message = TEXT("Cannot inport Speckle Objects, Speckle Unreal Manager has no converter components.");
		GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green, message);
		
	}
	else
	{
		ImportObjectFromCache(SpeckleObjects[ObjectID]);
	}
	
	for (auto& m : CreatedObjects)
	{
		if (InProgressObjects.Contains(m.Key) && InProgressObjects[m.Key] == m.Value)
			continue;

		m.Value->ConditionalBeginDestroy();
	}

	CreatedObjects = InProgressObjects;
	InProgressObjects.Empty();
	
	GEngine->AddOnScreenDebugMessage(0, 5.0f, FColor::Green, FString::Printf(TEXT("[Speckle] Objects imported successfully. Created %d Actors"), CreatedObjects.Num()));

}

void ASpeckleUnrealManager::AddConverter(UMeshConverter* MeshConverter)
{
	Converters.Add(MeshConverter->GetSpeckleType(), MeshConverter);
}

int32 ASpeckleUnrealManager::RemoveConverter(FString& SpeckleType)
{
	return Converters.Remove(SpeckleType);
}

void ASpeckleUnrealManager::DeleteObjects()
{
	for(const auto& C : Converters)
	{
		C.Value->DeleteObjects();		
	}
	
	for (const auto& m : CreatedObjects)
	{
		m.Value->ConditionalBeginDestroy();
	}

	CreatedObjects.Empty();
	InProgressObjects.Empty();
}


