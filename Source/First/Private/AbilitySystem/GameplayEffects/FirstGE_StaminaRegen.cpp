// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegen.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaRegen::UFirstGE_StaminaRegen()
{
	// 1. 无限持续：应用后一直生效，直到被主动移除。
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	// 2. 周期：每 PeriodSeconds 秒执行一次下面的 Modifiers（数值在默认值面板可调）。
	Period = FScalableFloat(PeriodSeconds);

	// 3. 每次周期执行：Stamina +RegenPerPeriod。
	//    闪避消耗一次是 -20，默认配置（0.5s 一次 +10）≈ 每秒回复一次闪避的消耗量。
	FGameplayModifierInfo RegenModifier;
	RegenModifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	RegenModifier.ModifierOp = EGameplayModOp::Additive;
	RegenModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(RegenPerPeriod));

	Modifiers.Add(RegenModifier);

	UTargetTagRequirementsGameplayEffectComponent* Requirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(
			TEXT("NormalRegenTagRequirementsComponent"));
	GEComponents.Add(Requirements);

	// Blocking 存在时关掉 20/秒的普通恢复，避免与 2/秒慢恢复叠加。
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_Blocking);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

void UFirstGE_StaminaRegen::PostInitProperties()
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
