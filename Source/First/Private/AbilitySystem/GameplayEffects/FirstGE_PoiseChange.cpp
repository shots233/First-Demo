// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_PoiseChange.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAttributeSet.h"

UFirstGE_PoiseChange::UFirstGE_PoiseChange()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat SetByCallerMagnitude;
	SetByCallerMagnitude.DataTag =MyGameplayTags::Combat_SetByCaller_PoiseDelta;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetPoiseAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =FGameplayEffectModifierMagnitude(SetByCallerMagnitude);
	Modifiers.Add(Modifier);
}
