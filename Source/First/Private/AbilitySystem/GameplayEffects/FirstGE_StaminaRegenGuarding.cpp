// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenGuarding.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "MyGameplayTags.h"
UFirstGE_StaminaRegenGuarding::UFirstGE_StaminaRegenGuarding()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(PeriodSeconds);

	// 每 PeriodSeconds 秒 +RegenPerPeriod（默认 0.5s +1，即每秒 +2）。数值在默认值面板可调。
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(RegenPerPeriod));
	Modifiers.Add(Modifier);

	UTargetTagRequirementsGameplayEffectComponent* Requirements =CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("GuardingTagRequirementsComponent"));
	GEComponents.Add(Requirements);

	Requirements->OngoingTagRequirements.RequireTags.AddTag(MyGameplayTags::DK_Status_Blocking);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

void UFirstGE_StaminaRegenGuarding::PostInitProperties()
{
	Super::PostInitProperties();

	// 以可调参数为准，重写序列化来的 Period 与第一条 Modifier 的数值，
	// 保证在蓝图子类默认值面板修改后实际生效。
	Period = FScalableFloat(PeriodSeconds);
	if (!Modifiers.IsEmpty())
	{
		Modifiers[0].ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(RegenPerPeriod));
	}
}
