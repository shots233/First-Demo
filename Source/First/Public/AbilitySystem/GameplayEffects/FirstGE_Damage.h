// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_Damage.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGE_Damage : public UGameplayEffect
{
	GENERATED_BODY()
public:
	// 作用：构造一个 Instant 伤害 GE，并固定使用本项目的伤害 Execution Calculation。
	UFirstGE_Damage();
};
