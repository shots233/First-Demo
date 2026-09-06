// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_DodgeCost.h"

#include "AbilitySystem/FirstAttributeSet.h"

UFirstGE_DodgeCost::UFirstGE_DodgeCost()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo StaminaCostModifier;
	StaminaCostModifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	StaminaCostModifier.ModifierOp = EGameplayModOp::Additive;
	StaminaCostModifier.ModifierMagnitude =FGameplayEffectModifierMagnitude(FScalableFloat(-20.f));

	Modifiers.Add(StaminaCostModifier);
}
