// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"

#include "MyGameplayTags.h"

UFirstGE_BossCooldown::UFirstGE_BossCooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	// 时长由能力里的 ApplyCooldown 用同一个标签写入。
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = MyGameplayTags::Boss_SetByCaller_CooldownDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
