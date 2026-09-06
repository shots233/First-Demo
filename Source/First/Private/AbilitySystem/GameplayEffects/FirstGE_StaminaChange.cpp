#include "AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaChange::UFirstGE_StaminaChange()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FSetByCallerFloat SetByCallerMagnitude;
	SetByCallerMagnitude.DataTag =MyGameplayTags::Combat_SetByCaller_StaminaDelta;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =FGameplayEffectModifierMagnitude(SetByCallerMagnitude);

	Modifiers.Add(Modifier);
}