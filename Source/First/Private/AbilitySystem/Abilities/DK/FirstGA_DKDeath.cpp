#include "AbilitySystem/Abilities/DK/FirstGA_DKDeath.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/DKCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UFirstGA_DKDeath::UFirstGA_DKDeath()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Death);
	SetAssetTags(AssetTags);

	// 死亡是终局状态；不允许新的玩家动作启动。
	BlockAbilitiesWithTag.AddTag(MyGameplayTags::DK_Ability_Action);

	// 不要把 Shared.Status.Dead 放进 ActivationBlockedTags，
	// 否则正是这个标签触发死亡时，死亡 Ability 会把自己挡住。
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Shared_Status_Dead;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
	AbilityTriggers.Add(Trigger);
}

void UFirstGA_DKDeath::ActivateAbility(
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
		EndAbility(
			Handle,
			ActorInfo,
			ActivationInfo,
			true,
			true);
		return;
	}

	// 死亡可以全面取消；普通受击只能按 DK.Ability.Action 精确取消。
	ASC->CancelAllAbilities(this);

	// 清除角色层输入状态和目标锁定。
	DK->PrepareForDeath();

	if (UCharacterMovementComponent* Movement =
		DK->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// 保留胶囊对地面的碰撞，只忽略 Pawn，防止 BOSS 推动尸体。
	if (UCapsuleComponent* Capsule = DK->GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(
			ECC_Pawn,
			ECR_Ignore);
	}

	if (UAnimMontage* Montage = DK->GetDeathMontage())
	{
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::
			CreatePlayMontageAndWaitProxy(
				this,
				TEXT("DKDeathMontage"),
				Montage);

		// 死亡是终局状态：不绑定 Completed 去 EndAbility。
		// Ability 保持激活，持续阻止 DK.Ability.Action。
		MontageTask->ReadyForActivation();
	}
}
