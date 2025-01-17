﻿#pragma once
#include "SlateBasics.h"
#include "SlateExtras.h"

class FSpeckleStyle
{
public:
 static void Initialize();
 static void Shutdown();
 static TSharedPtr< class ISlateStyle > Get();
 static FName GetStyleSetName();

private:
 static FString InContent(const FString& RelativePath, const ANSICHAR* Extension);
private:
 static TSharedPtr<class FSlateStyleSet> SpeckleStyleSet;
};