// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_ExecutionDamage.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAttributeSet.h"

UFirstGE_ExecutionDamage::UFirstGE_ExecutionDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat SetByCallerMagnitude;
	SetByCallerMagnitude.DataTag =MyGameplayTags::Combat_SetByCaller_ExecutionDamage;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetDamageTakenAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =FGameplayEffectModifierMagnitude(SetByCallerMagnitude);
	Modifiers.Add(Modifier);
}
