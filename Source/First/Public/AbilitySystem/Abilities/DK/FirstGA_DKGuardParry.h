// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "TimerManager.h"
#include "FirstGA_DKGuardParry.generated.h"

class UAnimMontage;
class UAnimInstance;

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

	// ASC 先更新 Spec 的按键状态，再调用这两个入口；不再重复创建输入任务。
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

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
	void HandleGuardHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleParrySuccess(FGameplayEventData Payload);

	UFUNCTION()
	void HandleParryChainWindowOpened(FGameplayEventData Payload);

	UFUNCTION()
	void HandleParryChainWindowClosed(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	UFUNCTION()
	void HandleMontageSectionChanged(UAnimMontage* Montage, FName SectionName, bool bLooped);

	void CancelActiveAttackAbilities();
	void StartGuardPushback(const FGameplayEventData& Payload);
	void StopGuardPushback();
	bool MeetsGuardRequirements(const FGameplayAbilityActorInfo* ActorInfo) const;
	void RemoveBlockingTag();
	void RemoveParryWindowTag();
	void CloseParryChainWindow();
	void ClearBufferedParryInput();
	void TryConsumeBufferedParryInput();
	void UpdateParryMontageExit();
	void HandleParryPlaybackEnd();
	void RestoreParryAutoBlendOut();
	bool JumpToGuardSection(FName SectionName);
	void BeginGuardExit();
	void FinishGuard(bool bWasCancelled);

	// 首次按下、连锁接招共用同一个计时器；松开不缩短本轮判定。
	void ReopenParryWindow();

	bool bOwnsBlockingTag = false;
	bool bOwnsParryWindowTag = false;
	bool bOwnsParryChainWindowTag = false;
	bool bGuardInputHeld = false;
	bool bChainWindowConsumed = false;
	bool bGuardMontageCompleted = false;
	// 只表示正式收势/取消，不用它记录一次普通松键。
	bool bExitRequested = false;

	// 成功演出期间允许松开后重新点按；每次成功演出只消费一次连锁窗口。
	bool bParryRiposteActive = false;
	FTimerHandle ParryWindowTimerHandle;
	FTimerHandle ParryPlaybackEndTimerHandle;
	int32 ParryPlaybackInstanceID = INDEX_NONE;
	bool bSavedParryAutoBlendOut = true;
	// 只移除本技能创建的推退；源到时由移动组件自动清理。
	TOptional<uint16> GuardPushbackSourceID;
	double BufferedParryInputExpiresAt = -1.0;
	// 通知对象属于共享资产；窗口所有权必须保存在这个逐角色的技能实例中。
	TSet<TWeakObjectPtr<const UObject>> ActiveChainWindowSources;
	TWeakObjectPtr<UAnimInstance> GuardAnimInstance;
	bool bSavedMovementState = false;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveGuardMontage;
};
