#include "Components/Combat/FirstBossRetaliationComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Character/DKCharacter.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"

namespace FirstBossRetaliation
{
	const TArray<FGameplayTag>& ResetTags()
	{
		static const TArray<FGameplayTag> Tags = { MyGameplayTags::Boss_Status_Attacking, MyGameplayTags::Boss_Status_DrawSword,
			MyGameplayTags::Boss_Status_Executable, MyGameplayTags::Boss_Status_BeingExecuted,
			MyGameplayTags::Boss_Status_Executed, MyGameplayTags::Shared_Status_Dead };
		return Tags;
	}
}

UFirstBossRetaliationComponent::UFirstBossRetaliationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFirstBossRetaliationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority()) return;
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!ASC) return;
	AbilitySystem = ASC;
	DamageDelegate = ASC->GenericGameplayEventCallbacks.FindOrAdd(MyGameplayTags::Shared_Event_DamageReceived)
		.AddUObject(this, &ThisClass::HandleDamageReceived);
	ActivatedDelegate = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::HandleAbilityActivated);
	for (FGameplayTag Tag : FirstBossRetaliation::ResetTags())
	{
		ResetTagDelegates.Add(Tag, ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleResetTag));
	}
}

void UFirstBossRetaliationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetPressure();
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		if (auto* Event = ASC->GenericGameplayEventCallbacks.Find(MyGameplayTags::Shared_Event_DamageReceived))
			Event->Remove(DamageDelegate);
		ASC->AbilityActivatedCallbacks.Remove(ActivatedDelegate);
		for (const auto& Binding : ResetTagDelegates)
			ASC->RegisterGameplayTagEvent(Binding.Key, EGameplayTagEventType::NewOrRemoved).Remove(Binding.Value);
	}
	ResetTagDelegates.Reset();
	CombatTarget.Reset();
	AbilitySystem.Reset();
	Super::EndPlay(EndPlayReason);
}

void UFirstBossRetaliationComponent::UpdateCombatContext(AActor* Target, bool bInCombat)
{
	AActor* NewTarget = bInCombat && IsValid(Target) && Target != GetOwner() ? Target : nullptr;
	if (CombatTarget.Get() != NewTarget || !NewTarget) ResetPressure();
	CombatTarget = NewTarget;
	if (!IsContextUsable() || HasProtectedState()) ResetPressure();
}

bool UFirstBossRetaliationComponent::HasActiveAbility(FGameplayTag AbilityTag) const
{
	const UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!ASC) return false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag)) return true;
	}
	return false;
}

bool UFirstBossRetaliationComponent::IsContextUsable() const
{
	const UAbilitySystemComponent* ASC = AbilitySystem.Get();
	AActor* Target = CombatTarget.Get();
	if (!bEnabled || HitsRequired < 1 || !FMath::IsFinite(HitResetDelay) || HitResetDelay < 0.1f ||
		!GetOwner()->HasAuthority() || !ASC || !IsValid(Target) || Target->IsActorBeingDestroyed() ||
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn)) return false;
	const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	return !TargetASC || !TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);
}

bool UFirstBossRetaliationComponent::HasProtectedState() const
{
	const UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!ASC) return true;
	for (FGameplayTag Tag : FirstBossRetaliation::ResetTags())
	{
		if (ASC->HasMatchingGameplayTag(Tag)) return true;
	}
	// 两种硬直共用 Staggered，必须检查实际能力来源；未知硬直同样不强行解除。
	return HasActiveAbility(MyGameplayTags::Boss_Ability_ParryStagger) ||
		(ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered) &&
		 !HasActiveAbility(MyGameplayTags::Boss_Ability_HitReact));
}

void UFirstBossRetaliationComponent::HandleDamageReceived(const FGameplayEventData* Payload)
{
	if (!Payload || Payload->Target != GetOwner() || !FMath::IsFinite(Payload->EventMagnitude) ||
		Payload->EventMagnitude <= 0.f || !IsContextUsable()) return;
	// 当前主角每段武器碰撞已去重；这里只统计带攻击能力来源的实际生命伤害。
	if (Payload->Instigator != CombatTarget.Get() || !Cast<ADKCharacter>(CombatTarget.Get()) ||
		!Payload->InstigatorTags.HasTag(MyGameplayTags::DK_Ability_Attack)) return;
	// 必须使用扣血前的状态，不能把“这一刀打断攻击”误算成非攻击时受击。
	for (FGameplayTag Tag : FirstBossRetaliation::ResetTags())
	{
		if (Payload->TargetTags.HasTag(Tag)) return;
	}
	if (HasProtectedState()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastHitTime >= 0.0 && Now - LastHitTime > HitResetDelay) ResetPressure();
	LastHitTime = Now;
	HitCount = FMath::Min(HitCount + 1, HitsRequired);
	GetWorld()->GetTimerManager().SetTimer(ResetTimer, this, &ThisClass::ResetPressure, HitResetDelay, false);
	if (HitCount >= HitsRequired && !bRetaliationPending && !PublishTimer.IsValid())
	{
		// 主角同一击先扣血、随后削韧。下一帧再发布，保证破韧/死亡先处理完。
		PublishTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::PublishRequest);
	}
}

void UFirstBossRetaliationComponent::PublishRequest()
{
	PublishTimer.Invalidate();
	if (!IsContextUsable() || HasProtectedState()) { ResetPressure(); return; }
	bRetaliationPending = HitCount >= HitsRequired && LastHitTime >= 0.0 &&
		GetWorld()->GetTimeSeconds() - LastHitTime < HitResetDelay;
}

bool UFirstBossRetaliationComponent::CanRetaliateAgainst(const AActor* Target) const
{
	return bRetaliationPending && HitCount >= HitsRequired && Target == CombatTarget.Get() &&
		IsContextUsable() && !HasProtectedState() && LastHitTime >= 0.0 &&
		GetWorld()->GetTimeSeconds() - LastHitTime < HitResetDelay;
}

bool UFirstBossRetaliationComponent::PrepareRetaliation(AActor* Target)
{
	if (!CanRetaliateAgainst(Target)) return false;
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	FGameplayTagContainer OrdinaryHitReact;
	OrdinaryHitReact.AddTag(MyGameplayTags::Boss_Ability_HitReact);
	ASC->CancelAbilities(&OrdinaryHitReact);
	// 不直接清除 Staggered，其他能力持有的硬直必须继续阻挡攻击。
	return CanRetaliateAgainst(Target) && !ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered);
}

void UFirstBossRetaliationComponent::HandleResetTag(FGameplayTag Tag, int32 Count)
{
	if (Count > 0) ResetPressure();
}

void UFirstBossRetaliationComponent::HandleAbilityActivated(UGameplayAbility* Ability)
{
	if (Ability && Ability->GetAssetTags().HasTagExact(MyGameplayTags::Boss_Ability_ParryStagger)) ResetPressure();
}

void UFirstBossRetaliationComponent::ResetPressure()
{
	HitCount = 0;
	bRetaliationPending = false;
	LastHitTime = -1.0;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ResetTimer);
		GetWorld()->GetTimerManager().ClearTimer(PublishTimer);
	}
}
