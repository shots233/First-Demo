// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_BossCooldown.generated.h"

/**
 * BOSS 招式冷却。时长由能力通过 SetByCaller 写入。
 */
UCLASS()
class FIRST_API UFirstGE_BossCooldown : public UGameplayEffect
{
	GENERATED_BODY()
	
public:
	UFirstGE_BossCooldown();
};
