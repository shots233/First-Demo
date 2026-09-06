// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "Types/FirstCombatTypes.h"
#include "GA_Boss_RetreatChargedSlash.generated.h"

class ACharacter;

/**
 * BOSS 后撤蓄力斩。
 * 后撤阶段不建立 AttackTarget；ChargeBegin 后预热并跟踪目标，
 * Montage 发出 Commit 事件后刷新固定快照并锁定攻击方向。
 */
UCLASS()
class FIRST_API UGA_Boss_RetreatChargedSlash : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_RetreatChargedSlash();

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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

	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	UFUNCTION()
	void HandleChargeBegin(FGameplayEventData Payload);

	UFUNCTION()
	void HandleAttackCommit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

private:
	ACharacter* ResolveCurrentTarget(const ABossCharacter* Boss) const;
	bool IsTargetUsable(const ACharacter* Target) const;
	void SetAttackDirectionLocked(bool bShouldLock);
	void DisableWeaponAttackEffects();

	UPROPERTY(EditDefaultsOnly, Category="Combat|Motion Warping")
	FFirstAttackWarpingData AttackWarpingData;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Retreat",
		meta=(ClampMin="0.0", Units="cm"))
	float RetreatCheckDistance = 220.f;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Retreat",
		meta=(ClampMin="0.0", Units="cm"))
	float RetreatNavTolerance = 50.f;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Retreat",
		meta=(ClampMin="0.0", Units="cm"))
	float RetreatMaxLandingHeightDelta = 60.f;

	FGameplayTagContainer CooldownTags;
	TWeakObjectPtr<ACharacter> CommittedTarget;

	bool bCommitReceived = false;
	bool bHitResolved = false;
	bool bDirectionLockApplied = false;
};
