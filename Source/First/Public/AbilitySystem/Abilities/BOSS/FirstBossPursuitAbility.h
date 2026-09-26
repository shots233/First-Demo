#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "TimerManager.h"
#include "FirstBossPursuitAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class URootMotionModifier;
struct FFirstBossPursuitSettings;

// 行为树远追分支启动的独立技能；前闪和刺击共用一次 GA 生命周期。
UCLASS(meta=(DisplayName="Boss Chase Pursuit"))
class FIRST_API UFirstBossPursuitAbility : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UFirstBossPursuitAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayEventData* Payload) const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;

private:
	enum class EPhase : uint8 { Dodge, Attack };
	EPhase Phase = EPhase::Dodge;
	bool bWasParried = false;
	bool bOwnsDirectionLock = false;
	bool bEndingPursuit = false;
	FGameplayTagContainer CooldownTags;
	FVector DashDestination = FVector::ZeroVector;
	float CommittedYaw = 0.f;
	TWeakObjectPtr<ACharacter> PursuitTarget;
	FTimerHandle StartTimer;
	FTimerHandle DashSafetyTimer;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActivePhaseMontage;
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveDodgeMontage;
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PhaseMontageTask;
	UPROPERTY()
	TObjectPtr<URootMotionModifier> DashWarpModifier;

	bool IsPursuitTargetValid(ACharacter* Target) const;
	bool IsTargetUsable(ACharacter* Target, float MaxRange) const;
	// 目标由调用者传入；技能只检查执行条件，不读取行为树黑板或追击策略。
	bool GetPursuitPlan(const FGameplayAbilityActorInfo* ActorInfo, ACharacter* Target,
		FVector& OutDirection, float& OutTravel) const;
	bool IsPursuitAllowed() const;
	bool AreSettingsValid(const FFirstBossPursuitSettings& Settings) const;
	void StartPursuit();
	void StartFollowupAttack();
	void MonitorDash();
	void PlayPhaseMontage(UAnimMontage* Montage, float PlayRate = 1.f);
	void ClearMontageTask();
	void StopCurrentPhase();
	void ClearDashWarp();
	void SetDirectionLocked(bool bLocked);
	void FinishPursuit(bool bCancelled);

	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);
	UFUNCTION()
	void HandleParried(FGameplayEventData Payload);
	UFUNCTION()
	void HandlePhaseCompleted();
	UFUNCTION()
	void HandlePhaseInterrupted();
};
