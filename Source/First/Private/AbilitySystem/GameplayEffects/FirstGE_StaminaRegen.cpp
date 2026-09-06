// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegen.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaRegen::UFirstGE_StaminaRegen()
{
	// 1. 无限持续：应用后一直生效，直到被主动移除。
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	
	// 2. 周期：每 0.5 秒执行一次下面的 Modifiers。
	Period = FScalableFloat(0.5f);
	
	// 3. 每次周期执行：Stamina +10。
	//    闪避消耗一次是 -20，所以每 1 秒恢复量 ≈ 每次消耗量的 1 倍。
	FGameplayModifierInfo RegenModifier;
	RegenModifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	RegenModifier.ModifierOp = EGameplayModOp::Additive;
	RegenModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.f));
	
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
