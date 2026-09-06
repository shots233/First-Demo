// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Abilities/Boss/GA_Boss_Death.h"

#include "AIController.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UGA_Boss_Death::UGA_Boss_Death()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Death);
	SetAssetTags(AssetTags);

	// 死亡标签一出现就自动激活（GAS 原生触发源）。
	// 【UE 5.6】旧名 FGameplayAbilityTrigger 已在 5.6 改名为 FAbilityTriggerData。
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Shared_Status_Dead;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_Death::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const bool bKilledByExecution = ASC && ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed);
	ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	if (!Boss)
	{
		FinishDeath(true);
		return;
	}

	// 普通死亡取消其它能力；处决死亡保留正在播放的配对 Target Montage。
	if (ASC && !bKilledByExecution)
	{
		ASC->CancelAllAbilities(this);
	}

	// 2. 停止移动（角色自身 + AI 控制器）。
	Boss->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AIController = Cast<AAIController>(Boss->GetController()))
	{
		AIController->StopMovement();
	}
	
	if (bKilledByExecution)
	{
		// Target Montage 自己包含死亡演出，并关闭了 Auto Blend Out。
		// 这里不再播放通用 DeathMontage，也不取消 Executable Ability。
		FinishDeath(false);
		return;
	}
	
	// 3. 播放死亡蒙太奇。
	UAnimMontage* Montage = Boss->GetDeathMontage();
	if (!Montage)
	{
		FinishDeath(true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("DeathMontage"),
			Montage);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_Death::HandleMontageCompleted()
{
	FinishDeath(false);
}

void UGA_Boss_Death::HandleMontageInterrupted()
{
	FinishDeath(true);
}

void UGA_Boss_Death::FinishDeath(bool bWasCancelled)
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
