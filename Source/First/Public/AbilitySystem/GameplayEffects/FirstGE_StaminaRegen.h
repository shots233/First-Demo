// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_StaminaRegen.generated.h"

/**
 * 精力自动恢复。
 *
 * 无限持续时间 + 周期性执行：
 * 每 Period 秒给 Stamina 添加 RegenPerPeriod 数值。
 * 超过 MaxStamina 的部分由 AttributeSet 的夹紧逻辑自动截断。
 */
UCLASS()
class FIRST_API UFirstGE_StaminaRegen : public UGameplayEffect
{
	GENERATED_BODY()
	
public:
	UFirstGE_StaminaRegen();
	
};
