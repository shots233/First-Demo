#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "MyGameplayTags.h"

UFirstGA_DKGuardBreak::UFirstGA_DKGuardBreak()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Action);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_GuardBreak);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::DK_Event_GuardBroken;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UFirstGA_DKGuardBreak::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	UDKDefenseComponent* Defense =
		DK ? DK->GetDKDefenseComponent() : nullptr;

	if (!DK || !ASC || !Defense)
	{
		FinishGuardBreak(true);
		return;
	}

	// 破防事件可能与 Space 输入落在同一帧；禁止恢复移动后补跳。
	DK->StopJumping();

	// 取消长期 Guard 会通过它自己的 EndAbility 立即移除 Blocking/ParryWindow。
	FGameplayTagContainer AbilitiesToCancel;
	AbilitiesToCancel.AddTag(MyGameplayTags::DK_Ability_GuardParry);
	AbilitiesToCancel.AddTag(MyGameplayTags::DK_Ability_Attack);
	AbilitiesToCancel.AddTag(MyGameplayTags::DK_Ability_Dodge);
	ASC->CancelAbilities(&AbilitiesToCancel, nullptr, this);

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		SavedMovementMode = Movement->MovementMode;
		SavedCustomMovementMode = Movement->CustomMovementMode;
		bSavedMovementMode = true;

		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	const float BreakDuration = FMath::Max(
		Defense->GetGuardBreakDuration(),
		0.01f);

	if (UAnimMontage* Montage = DK->GetGuardBreakMontage())
	{
		// Delay 是玩法权威时长；按动画长度计算 Rate，让表现尽量同步。
		const float PlayRate = Montage->GetPlayLength() > 0.f? Montage->GetPlayLength() / BreakDuration: 1.f;

		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,TEXT("GuardBreakMontage"),Montage,PlayRate);
		MontageTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* DelayTask =
		UAbilityTask_WaitDelay::WaitDelay(this, BreakDuration);
	DelayTask->OnFinish.AddDynamic(
		this,
		&ThisClass::HandleBreakFinished);
	DelayTask->ReadyForActivation();
}

void UFirstGA_DKGuardBreak::HandleBreakFinished()
{
	FinishGuardBreak(false);
}

void UFirstGA_DKGuardBreak::FinishGuardBreak(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

void UFirstGA_DKGuardBreak::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	const bool bIsDead = ASC && ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	if (DK && bSavedMovementMode && !bIsDead)
	{
		if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
		{
			Movement->MaxWalkSpeed = DK->GetDesiredLocomotionSpeed();
			Movement->SetMovementMode(
				SavedMovementMode,
				SavedCustomMovementMode);
		}
	}

	bSavedMovementMode = false;

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}