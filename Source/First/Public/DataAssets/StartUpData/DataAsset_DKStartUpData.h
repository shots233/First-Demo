// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/StartUpData/DataAsset_StartUpDataBase.h"
#include "Types/FirstStructTypes.h"
#include "DataAsset_DKStartUpData.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UDataAsset_DKStartUpData : public UDataAsset_StartUpDataBase
{
	GENERATED_BODY()
public:
	// 作用：在基类出生配置完成后，再授予 DK 特有且带输入 Tag 的装备、卸下和闪避技能。
	virtual void GiveToAbilitySystemComponent(UFirstAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1) override;
	
private:
	UPROPERTY(EditDefaultsOnly, Category="StartupData", meta=(TitleProperty="InputTag"))
	TArray<FFirstDKAbilitySet> DKStartUpAbilitySets;
	
};
