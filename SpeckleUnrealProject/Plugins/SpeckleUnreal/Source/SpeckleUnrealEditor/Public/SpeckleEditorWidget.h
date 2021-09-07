// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "EngineUtils.h"
#include "SpeckleUnreal/Public/ISpeckleReceiver.h"
#include "SpeckleEditorWidget.generated.h"

class ASpeckleUnrealManager;


UCLASS()
class USpeckleEditorWidget : public UEditorUtilityWidget
{
	GENERATED_BODY()
	
protected:
	virtual void NativeConstruct() override;
	
public:
	// BUTTONS
    UPROPERTY()
    class UButton* ImportObjectButton;

    UPROPERTY()
    UButton* FetchButton;
	
	//TEXT BLOCKS
    UPROPERTY()
    class UTextBlock* StreamName;

	UPROPERTY()
	class UTextBlock* SelectedCommitText;

	//COMBO BOXES
    UPROPERTY()
    class UComboBoxString* BranchesCBox;
    
    UPROPERTY()
    class UComboBoxString* CommitsCBox;

	UPROPERTY()
	class UComboBoxString* SpeckleManagersCBox;

	//FUNCTIONS
	void InitUI();

	UFUNCTION(BlueprintCallable)
	void ImportSpeckleObject(UActorComponent* SpeckleReceiver);

	UFUNCTION(BlueprintCallable)
    void FetchSpeckleCommits(UActorComponent* SpeckleReceiver);

	UFUNCTION(BlueprintCallable)
    void SpeckleCommitsReceived(const TArray<FSpeckleCommit>& CommitsList);

	UFUNCTION(BlueprintCallable)
    void FetchSpeckleBranches(UActorComponent* SpeckleReceiver);

	UFUNCTION(BlueprintCallable)
    void SpeckleBranchesReceived(const TArray<FSpeckleBranch>& BranchesList);

	UFUNCTION(BlueprintCallable)
	void SpeckleManagerSelectionListener(FString SelectedItem, ESelectInfo::Type SelectionType);

	FString SelectedBranch;
	
	UFUNCTION(BlueprintCallable)
	void BranchesBoxSelectionListener(FString SelectedItem, ESelectInfo::Type SelectionType)
	{
		SelectedBranch = SelectedItem;
	}

	template<typename T>
	UFUNCTION(BlueprintCallable)
	void FindAllActors(UWorld* World, TArray<T*>& Out)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
			Out.Add(*It);
		}
	}

    UPROPERTY(EditAnywhere)
    TSubclassOf<ASpeckleUnrealManager> SpeckleManagerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ASpeckleUnrealManager* CurrentSpeckleManager;

	UPROPERTY()
	TArray<ASpeckleUnrealManager*> SpeckleManagers;
	
};
