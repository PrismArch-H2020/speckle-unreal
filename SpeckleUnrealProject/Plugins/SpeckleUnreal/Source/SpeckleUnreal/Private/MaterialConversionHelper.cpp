﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "MaterialConversionHelper.h"
#include "RenderMaterial.h"

void FMaterialConversionHelper::AssignPropertiesFromSpeckle(UMaterialInstanceDynamic* Material, const URenderMaterial* SpeckleMaterial)
{
	Material->SetScalarParameterValue("Opacity", SpeckleMaterial->Opacity);
	Material->SetScalarParameterValue("Metallic", SpeckleMaterial->Metalness);
	Material->SetScalarParameterValue("Roughness", SpeckleMaterial->Roughness);
	Material->SetVectorParameterValue("BaseColor", SpeckleMaterial->Diffuse);
	Material->SetVectorParameterValue("EmissiveColor", SpeckleMaterial->Emissive);
}

URenderMaterial* FMaterialConversionHelper::ParseRenderMaterial(const TSharedPtr<FJsonObject> SpeckleObject)
{
	URenderMaterial* RenderMaterial = NewObject<URenderMaterial>();;
	
	SpeckleObject->TryGetStringField("id", RenderMaterial->ObjectID);
	SpeckleObject->TryGetStringField("name", RenderMaterial->Name);
	SpeckleObject->TryGetNumberField("opacity", RenderMaterial->Opacity);
	SpeckleObject->TryGetNumberField("metalness", RenderMaterial->Metalness);
	SpeckleObject->TryGetNumberField("roughness", RenderMaterial->Roughness);
	
	int32 ARGB;
	if(SpeckleObject->TryGetNumberField("diffuse", ARGB))
		RenderMaterial->Diffuse = FColor(ARGB);
		
	if(SpeckleObject->TryGetNumberField("emissive", ARGB))
		RenderMaterial->Emissive = FColor(ARGB);
	
	return RenderMaterial;
}
