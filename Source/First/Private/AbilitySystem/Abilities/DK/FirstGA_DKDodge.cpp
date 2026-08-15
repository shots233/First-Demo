#include "AbilitySystem/Abilities/DK/FirstGA_DKDodge.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
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
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Dodge);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	CostGameplayEffectClass = UFirstGE_DodgeCost::StaticClass();
}

bool UFirstGA_DKDodge::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// 保留 GAS 的默认检查：死亡、已在闪避、切换武器、Cost、Cooldown 等。
	if (!Super::CanActivateAbility(Handle,ActorInfo,SourceTags,TargetTags,OptionalRelevantTags))
	{
		return false;
	}
	
	const UAbilitySystemComponent* ASC = ActorInfo? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	
	if (!ASC)
	{
		return false;
	}
	
	const bool bIsAttacking = ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Attacking);
	
	// 不在攻击中时，Dodge 不需要任何取消权限。
	if (!bIsAttacking)
	{
		return true;
	}
	// 攻击中时，只接受动画窗口明确授予的 Dodge 权限。
	return ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Action_Cancelable_By_Dodge);
}

// 作用：在 Cost 检查通过后执行方向闪避，并用 AbilityTask 管理动画或延时生命周期。
void UFirstGA_DKDodge::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DKCharacter = GetDKCharacterFromActorInfo();
	if (!DKCharacter)
	{
		FinishDodge(true);
		return;
	}

	// CommitAbility 统一执行 Cost/Cooldown 检查和应用。体力不足时不会先移动再失败。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishDodge(true);
		return;
	}

	// 到这里说明闪避的 Cost / Cooldown 已经提交成功。
	// 现在才取消攻击，避免体力不足时错误中断攻击。
	CancelActiveAttackAbilities();
	
	FVector DodgeDirection = DKCharacter->GetLastMovementInputVector();
	DodgeDirection.Z = 0.f;
	if (DodgeDirection.IsNearlyZero())
	{
		DodgeDirection = DKCharacter->GetActorForwardVector();
		DodgeDirection.Z = 0.f;
	}
	DodgeDirection.Normalize();

	UAnimMontage* DodgeMontage = DKCharacter->GetDodgeMontage();
	const bool bAnimationProvidesMovement =
		DodgeMontage && DKCharacter->DoesDodgeUseRootMotion();

	// Root Motion 与 LaunchCharacter 只能选择一个位移来源，否则距离会叠加。
	if (!bAnimationProvidesMovement)
	{
		DKCharacter->LaunchCharacter(
			DodgeDirection * DKCharacter->GetDodgeImpulse(),
			true,
			false);
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
		DKCharacter->GetDodgeDuration());
	DelayTask->OnFinish.AddDynamic(this, &ThisClass::HandleDodgeCompleted);
	DelayTask->ReadyForActivation();
}

void UFirstGA_DKDodge::CancelActiveAttackAbilities()
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	
	if (!ASC)
	{
		return;
	}
	
	FGameplayTagContainer AttackAbilityTags;
	AttackAbilityTags.AddTag(MyGameplayTags::DK_Ability_Attack);

	// nullptr：不额外排除标签。
	// this：不要取消当前正要启动的 Dodge Ability。
	ASC->CancelAbilities(&AttackAbilityTags, nullptr, this);
}

// 作用：正常结束闪避；GAS 随 EndAbility 自动移除 DK.Status.Dodging。
void UFirstGA_DKDodge::HandleDodgeCompleted()
{
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