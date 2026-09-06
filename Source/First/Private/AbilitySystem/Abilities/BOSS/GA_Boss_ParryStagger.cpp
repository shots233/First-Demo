#include "AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.h"

#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UGA_Boss_ParryStagger::UGA_Boss_ParryStagger()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_ParryStagger);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_Parried;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_ParryStagger::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponentFromActorInfo();
	if (!Boss || !ASC || !Boss->GetFirstAttributeSet())
	{
		FinishParryStagger(true);
		return;
	}

	// 霸体分支（四连斩等）：弹反照常削韧，但不取消招式、不进僵直。
	// 注意不能用 ActivationBlockedTags 拦整个能力——削韧逻辑就在本能力内部，
	// 拦掉激活等于霸体期间弹反不削韧，违反设计决策 D1。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Uninterruptible))
	{
		const float SuperArmorPoiseDamage =
			TriggerEventData ? FMath::Max(TriggerEventData->EventMagnitude, 0.f) : 50.f;
		AActor* SuperArmorPoiseSource =
			TriggerEventData ? const_cast<AActor*>(TriggerEventData->Instigator.Get()) : nullptr;

		const EFirstPoiseDamageResult SuperArmorPoiseResult =
			Boss->ApplyPoiseDamage(SuperArmorPoiseDamage, SuperArmorPoiseSource);

		// 破韧（D2）：Boss_Event_PoiseBroken → Executable 在同一调用栈内同步取消
		// 包括本能力在内的攻击链。与主流程一致，退出前检查生命周期。
		if (!IsActive())
		{
			return;
		}

		if (SuperArmorPoiseResult == EFirstPoiseDamageResult::Ignored)
		{
			// 与主流程口径一致：没有发生实际削韧时不伪装成正常结束。
			FinishParryStagger(true);
			return;
		}

		// Broken（未被 Executable 取消时）与 Reduced：静默结束，不播僵直，四连斩继续。
		FinishParryStagger(false);
		return;
	}

	// 先取消整套攻击。攻击 EndAbility 会再次兜底关闭碰撞。
	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	ASC->CancelAbilities(&AttackTags, nullptr, this);

	if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
	{
		Combat->ToggleWeaponCollision(false);
	}

	Boss->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AIController = Cast<AAIController>(Boss->GetController()))
	{
		AIController->StopMovement();
	}

	const float PoiseDamage = TriggerEventData? FMath::Max(TriggerEventData->EventMagnitude, 0.f): 50.f;

	AActor* PoiseSource = TriggerEventData? const_cast<AActor*>(TriggerEventData->Instigator.Get()): nullptr;

	const EFirstPoiseDamageResult PoiseResult =
		Boss->ApplyPoiseDamage(PoiseDamage, PoiseSource);

	// PoiseBroken 会同步激活 Executable；Executable 又可能立刻取消
	// 当前 ParryStagger。返回后先检查生命周期，不能继续创建旧任务。
	if (!IsActive())
	{
		return;
	}

	if (PoiseResult == EFirstPoiseDamageResult::Broken)
	{
		// 归零事件已经由 Boss 统一发送，不再叠加 1 秒僵直。
		FinishParryStagger(false);
		return;
	}

	if (PoiseResult == EFirstPoiseDamageResult::Ignored)
	{
		// 没有发生实际削韧时，不能伪装成普通 Reduced。
		FinishParryStagger(true);
		return;
	}

	// 100→50：只播放普通弹反僵直。
	const float StaggerDuration = FMath::Max(Boss->GetParryStaggerDuration(),0.01f);

	if (UAnimMontage* Montage = Boss->GetParryStaggerMontage())
	{
		const float PlayRate = Montage->GetPlayLength() > 0.f? Montage->GetPlayLength() / StaggerDuration: 1.f;

		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,TEXT("BossParryStaggerMontage"),Montage,PlayRate);
		MontageTask->ReadyForActivation();
	}

	// 玩法状态严格由 1.0 秒 Delay 决定，不由动画原始长度决定。
	UAbilityTask_WaitDelay* DelayTask =UAbilityTask_WaitDelay::WaitDelay(this, StaggerDuration);
	DelayTask->OnFinish.AddDynamic(this,&ThisClass::HandleStaggerFinished);
	DelayTask->ReadyForActivation();
}

void UGA_Boss_ParryStagger::HandleStaggerFinished()
{
	FinishParryStagger(false);
}

void UGA_Boss_ParryStagger::FinishParryStagger(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bWasCancelled);
}