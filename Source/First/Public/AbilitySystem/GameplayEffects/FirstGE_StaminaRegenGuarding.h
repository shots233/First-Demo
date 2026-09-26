// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_StaminaRegenGuarding.generated.h"

/**
 * 格挡中的精力慢恢复（默认每秒 +2）。
 * 要求持有 DK.Status.Blocking，与 FirstGE_StaminaRegen 靠标签互斥，不会叠加。
 */
UCLASS()
class FIRST_API UFirstGE_StaminaRegenGuarding : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UFirstGE_StaminaRegenGuarding();

	// 蓝图子类的默认值不走本构造函数，序列化完成后在这里把可调参数同步进 Period/Modifiers。
	virtual void PostInitProperties() override;

protected:
	// 每个周期恢复的精力值。在蓝图子类（或 DA 引用类的 Class Defaults）中可调，不要直接改 Modifiers 数组。
	UPROPERTY(EditDefaultsOnly, Category="Stamina|Regen", meta=(ClampMin="0.0"))
	float RegenPerPeriod = 1.f;

	// 恢复周期，单位秒。实际每秒回复量 ≈ RegenPerPeriod / PeriodSeconds。
	UPROPERTY(EditDefaultsOnly, Category="Stamina|Regen", meta=(ClampMin="0.01"))
	float PeriodSeconds = 0.5f;
};
