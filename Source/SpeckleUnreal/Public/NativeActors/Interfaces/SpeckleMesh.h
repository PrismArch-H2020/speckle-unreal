﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpeckleMesh.generated.h"

class ASpeckleUnrealManager;
class UMesh;

// This class does not need to be modified.
UINTERFACE()
class USpeckleMesh : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SPECKLEUNREAL_API ISpeckleMesh
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION(BlueprintNativeEvent)
	void SetMesh(const UMesh* SpeckleMesh, ASpeckleUnrealManager* Manager);
	
};
