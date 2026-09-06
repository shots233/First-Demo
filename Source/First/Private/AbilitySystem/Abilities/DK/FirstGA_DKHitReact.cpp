#include "AbilitySystem/Abilities/DK/FirstGA_DKHitReact.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "MyGameplayTags.h"

UFirstGA_DKHitReact::UFirstGA_DKHitReact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_HitReact);
	SetAssetTags(AssetTags);

	// 激活后自动取消当前玩家动作，并在整个受击期间阻止新动作。
	CancelAbilitiesWithTag.AddTag(MyGameplayTags::DK_Ability_Action);
	BlockAbilitiesWithTag.AddTag(MyGameplayTags::DK_Ability_Action);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_HitReact);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_HitReact);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Shared_Event_DamageReceived;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UFirstGA_DKHitReact::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	if (!DK || !ASC)
	{
		FinishHitReact(true);
		return;
	}

	DK->StopJumping();

	if (UCharacterMovementComponent* Movement =
		DK->GetCharacterMovement())
	{
		SavedMovementMode = Movement->MovementMode;
		SavedCustomMovementMode = Movement->CustomMovementMode;
		bSavedMovementMode = true;

		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	const AActor* DamageInstigator = TriggerEventData
		? TriggerEventData->Instigator.Get()
		: nullptr;

	UAnimMontage* Montage =
		SelectHitReactMontage(DK, DamageInstigator);

	const float HitReactDuration =
		FMath::Max(DK->GetHitReactDuration(), 0.05f);

	if (Montage)
	{
		// 玩法硬直由 HitReactDuration 决定；四个动画长度不同时也保持一致。
		const float PlayRate = Montage->GetPlayLength() > 0.f
			? Montage->GetPlayLength() / HitReactDuration
			: 1.f;

		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::
			CreatePlayMontageAndWaitProxy(
				this,
				TEXT("DKHitReactMontage"),
				Montage,
				PlayRate);

		MontageTask->ReadyForActivation();
	}

	// WaitDelay 是玩法权威时长：有没有动画、动画原始长度多少，
	// 都严格在 HitReactDuration 后结束硬直。
	UAbilityTask_WaitDelay* DelayTask =
		UAbilityTask_WaitDelay::WaitDelay(
			this,
			HitReactDuration);
	DelayTask->OnFinish.AddDynamic(
		this,
		&ThisClass::HandleHitReactFinished);
	DelayTask->ReadyForActivation();
}

UAnimMontage* UFirstGA_DKHitReact::SelectHitReactMontage(
	const ADKCharacter* DK,
	const AActor* DamageInstigator) const
{
	if (!DK)
	{
		return nullptr;
	}

	// 环境伤害、来源丢失或双方重合时，稳定回退到正面受击。
	if (!IsValid(DamageInstigator) || DamageInstigator == DK)
	{
		return DK->GetHitReactFrontMontage();
	}

	FVector ToAttacker =
		DamageInstigator->GetActorLocation() - DK->GetActorLocation();
	ToAttacker.Z = 0.f;

	if (ToAttacker.IsNearlyZero())
	{
		return DK->GetHitReactFrontMontage();
	}

	ToAttacker.Normalize();

	FVector Forward = DK->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();

	FVector Right = DK->GetActorRightVector();
	Right.Z = 0.f;
	Right.Normalize();

	const float ForwardDot = FVector::DotProduct(Forward, ToAttacker);
	const float RightDot = FVector::DotProduct(Right, ToAttacker);

	UAnimMontage* SelectedMontage = nullptr;

	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		SelectedMontage = ForwardDot >= 0.f
			? DK->GetHitReactFrontMontage()
			: DK->GetHitReactBackMontage();
	}
	else
	{
		SelectedMontage = RightDot >= 0.f
			? DK->GetHitReactRightMontage()
			: DK->GetHitReactLeftMontage();
	}

	// 某一个方向忘记配置时，不让能力失败，统一回退到 Front。
	return SelectedMontage
		? SelectedMontage
		: DK->GetHitReactFrontMontage();
}

void UFirstGA_DKHitReact::HandleHitReactFinished()
{
	FinishHitReact(false);
}

void UFirstGA_DKHitReact::FinishHitReact(bool bWasCancelled)
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

void UFirstGA_DKHitReact::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	const bool bIsDead = ASC &&
		ASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

	// 被死亡能力打断时绝对不能恢复移动。
	if (DK && bSavedMovementMode && !bIsDead)
	{
		if (UCharacterMovementComponent* Movement =
			DK->GetCharacterMovement())
		{
			Movement->MaxWalkSpeed =
				DK->GetDesiredLocomotionSpeed();
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
