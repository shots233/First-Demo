// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKGuardParry.generated.h"

class UAnimMontage;

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGA_DKGuardParry : public UFirstDKGameplayAbility
{
	GENERATED_BODY()
public:
	UFirstGA_DKGuardParry();

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

private:
	UFUNCTION()
	void HandleParryWindowElapsed();

	UFUNCTION()
	void HandleInputReleased(float TimeHeld);

	UFUNCTION()
	void HandleGuardHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleParrySuccess(FGameplayEventData Payload);

	// 连锁弹反：连锁窗口（ANS_ParryChainWindow）内按下弹反键时由角色层发来。
	UFUNCTION()
	void HandleParryChainPressed(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void CancelActiveAttackAbilities();
	void RemoveBlockingTag();
	void RemoveParryWindowTag();
	void JumpToGuardSection(FName SectionName);
	void BeginGuardExit();
	void FinishGuard(bool bWasCancelled);

	// 连锁弹反时重开一轮 0.15s 弹反窗口（重挂标签 + 重启窗口计时器）。
	void ReopenParryWindow();

	bool bOwnsBlockingTag = false;
	bool bOwnsParryWindowTag = false;
	bool bExitRequested = false;

	// 弹反成功后的反击演出进行中：松键只摘防御标签、不再跳 End 段截断动画，
	// Parry 段完整播完后由 HandleMontageCompleted 统一收势（方案 A）。
	bool bParryRiposteActive = false;
	bool bSavedMovementState = false;
	bool bTargetLockWasActiveAtStart = false;

	bool bSavedOrientRotationToMovement = true;
	bool bSavedUseControllerDesiredRotation = false;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveGuardMontage;
};
