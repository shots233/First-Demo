// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenGuarding.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "MyGameplayTags.h"
UFirstGE_StaminaRegenGuarding::UFirstGE_StaminaRegenGuarding()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(0.5f);

	// 每 0.5 秒 +1，即每秒 +2。
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	Modifiers.Add(Modifier);

	UTargetTagRequirementsGameplayEffectComponent* Requirements =CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("GuardingTagRequirementsComponent"));
	GEComponents.Add(Requirements);

	Requirements->OngoingTagRequirements.RequireTags.AddTag(MyGameplayTags::DK_Status_Blocking);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}
