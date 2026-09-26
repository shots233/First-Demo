#include "AbilitySystem/Abilities/DK/FirstGA_DKDodge.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/GameplayEffects/FirstGE_DodgeCost.h"
#include "Character/DKCharacter.h"
#include "MyGameplayTags.h"

// 作用：定义闪避的身份、执行期间状态、启动限制和体力消耗类。
UFirstGA_DKDodge::UFirstGA_DKDodge()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Action);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Dodge);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_DodgeExhausted);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	// 不要把 DK.Status.Defending 加进来：格挡可以被闪避取消。
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);

	CostGameplayEffectClass = UFirstGE_DodgeCost::StaticClass();
}

bool UFirstGA_DKDodge::CheckCost(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && ASC->GetNumericAttribute(UFirstAttributeSet::GetStaminaAttribute()) > 0.f &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_DodgeExhausted))
	{
		// 本技能的费用只有精力，允许不足额支付最后一次。
		// 原生 CheckCost 会拒绝扣费后为负的情况；实际仍用原 Cost GE 扣费，
		// 再由 AttributeSet 将剩余精力夹到 0，保留统一的属性和 UI 更新路径。
		return true;
	}

	const FGameplayTag& CostTag = UAbilitySystemGlobals::Get().ActivateFailCostTag;
	if (OptionalRelevantTags && CostTag.IsValid())
	{
		OptionalRelevantTags->AddTag(CostTag);
	}
	return false;
}

bool UFirstGA_DKDodge::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	// 这里也会检查死亡、重复闪避、换武器、Cost 和 Cooldown。
	if (!Super::CanActivateAbility(
			Handle,
			ActorInfo,
			SourceTags,
			TargetTags,
			OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;

	if (!ASC)
	{
		return false;
	}

	const bool bIsAttacking = ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Attacking);
	const bool bIsDefending = ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Defending);

	// 普通移动状态下不需要取消权限。
	if (!bIsAttacking && !bIsDefending)
	{
		return true;
	}

	// 攻击只在动画的 .By.Dodge 窗口授予该 Tag；
	// Guard 则在整个 Ability 生命周期持有该 Tag。
	return ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Action_Cancelable_By_Dodge);
}

// 作用：在 Cost 检查通过后执行方向闪避，并用 AbilityTask 管理动画或延时生命周期。
void UFirstGA_DKDodge::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABaseCharacter* BaseCharacter = GetBaseCharacterFromActorInfo();
	if (!BaseCharacter)
	{
		FinishDodge(true);
		return;
	}

	// 激活与提交都检查精力大于 0 且不在恢复锁中；成功后仍由原 Cost GE 扣费。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishDodge(true);
		return;
	}

	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
		ASC && ASC->GetNumericAttribute(UFirstAttributeSet::GetStaminaAttribute()) <= 0.f)
	{
		// 不足额支付和恰好耗尽都锁住后续闪避；锁由 ASC 持有，跨本次技能结束保留。
		ASC->StartDodgeStaminaRecovery();
	}

	// 成功 Commit 后才取消 Attack / Guard，避免启动被拒绝却丢了原来的动作。
	CancelDodgeInterruptibleAbilities();
	
	// 8 向：方向来源由角色提供（玩家=按键，BOSS=以后AI），再量化索引。
	CurrentDodgeDirectionIndex = BaseCharacter->GetDodgeDirectionIndex();

	FVector DodgeDirection = BaseCharacter->GetDodgeInputDirection();

	UAnimMontage* DodgeMontage =BaseCharacter->GetDodgeMontageForDirection(CurrentDodgeDirectionIndex);
	const bool bAnimationProvidesMovement =DodgeMontage && BaseCharacter->DoesDodgeUseRootMotion();

	if (!bAnimationProvidesMovement)
	{
		BaseCharacter->LaunchCharacter(DodgeDirection * BaseCharacter->GetDodgeImpulse(),true,false);
	}

	if (DodgeMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				TEXT("DodgeMontage"),
				DodgeMontage);

		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleDodgeCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleDodgeCancelled);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleDodgeCancelled);
		MontageTask->ReadyForActivation();
		return;
	}

	// 没有动画时仍保留一小段 Ability 生命周期，让 Dodging Tag 真实存在并可验收互斥。
	UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(
		this,
		BaseCharacter->GetDodgeDuration());
	DelayTask->OnFinish.AddDynamic(this, &ThisClass::HandleDodgeCompleted);
	DelayTask->ReadyForActivation();
}

void UFirstGA_DKDodge::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 所有清理必须发生在唯一一次 Super::EndAbility 之前。
	// DodgeExhausted 由 ASC 在精力回满时移除，不能随闪避完成或取消清理。
	if (UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_Invincible);
	}

	Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}

void UFirstGA_DKDodge::CancelDodgeInterruptibleAbilities()
{
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayTagContainer AbilitiesToCancel;
	AbilitiesToCancel.AddTag(
		MyGameplayTags::DK_Ability_Attack);
	AbilitiesToCancel.AddTag(
		MyGameplayTags::DK_Ability_GuardParry);

	// this 防止取消刚刚启动的 Dodge 自己。
	ASC->CancelAbilities(&AbilitiesToCancel, nullptr, this);
}

// 作用：正常结束闪避；GAS 随 EndAbility 自动移除 DK.Status.Dodging。
void UFirstGA_DKDodge::HandleDodgeCompleted()
{
	// 闪避动画正常结束后直接结束能力。
	// 长按奔跑由角色层计时器负责（SetRunning），这里不再播放衔接动画。
	FinishDodge(false);
}

// 作用：取消结束闪避；仍走同一 EndAbility 清理路径。
void UFirstGA_DKDodge::HandleDodgeCancelled()
{
	FinishDodge(true);
}

// 作用：只结束一次当前闪避 Ability。
void UFirstGA_DKDodge::FinishDodge(bool bWasCancelled)
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
