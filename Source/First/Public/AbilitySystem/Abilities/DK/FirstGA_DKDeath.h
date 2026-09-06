#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKDeath.generated.h"

/**
 * 主角死亡能力。
 * Shared.Status.Dead 被添加时自动激活；当前版本是终局状态，不在本 Ability 内复活。
 */
UCLASS()
class FIRST_API UFirstGA_DKDeath : public UFirstDKGameplayAbility
{
	GENERATED_BODY()

public:
	UFirstGA_DKDeath();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
