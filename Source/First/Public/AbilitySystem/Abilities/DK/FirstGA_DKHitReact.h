#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKHitReact.generated.h"

class ADKCharacter;
class UAnimMontage;

/**
 * 主角普通掉血受击。
 * 由 Shared.Event.DamageReceived 触发；根据事件 Instigator 选择四方向 Montage。
 */
UCLASS()
class FIRST_API UFirstGA_DKHitReact : public UFirstDKGameplayAbility
{
	GENERATED_BODY()

public:
	UFirstGA_DKHitReact();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleHitReactFinished();

	void FinishHitReact(bool bWasCancelled);

	UAnimMontage* SelectHitReactMontage(
		const ADKCharacter* DK,
		const AActor* DamageInstigator) const;

	bool bSavedMovementMode = false;
	TEnumAsByte<EMovementMode> SavedMovementMode = MOVE_Walking;
	uint8 SavedCustomMovementMode = 0;
};
