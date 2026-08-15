// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_DodgeCost.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGE_DodgeCost : public UGameplayEffect
{
	GENERATED_BODY()
public:
	// 作用：构造闪避的 Instant 体力消耗，供 CommitAbility 统一检查并应用。
	UFirstGE_DodgeCost();
};
