// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "MyGameplayTags.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

ABossCharacter* UFirstBossGameplayAbility::GetBossCharacterFromActorInfo()
{
	return Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
}

UBossCombatComponent* UFirstBossGameplayAbility::GetBossCombatComponentFromActorInfo()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	return Boss ? Boss->GetBossCombatComponent() : nullptr;
}

FGameplayEffectSpecHandle UFirstBossGameplayAbility::MakeBossDamageEffectSpecHandle(
	TSubclassOf<UGameplayEffect> EffectClass, float InBaseDamage)
{
	check(EffectClass);

	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	check(ASC);

	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.SetAbility(this);
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, GetAbilityLevel(), ContextHandle);

	// 复用玩家伤害公式的 SetByCaller 标签；连击段数固定 1（BOSS 三连的加成以后单独定）。
	SpecHandle.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_BaseDamage, InBaseDamage);
	SpecHandle.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_ComboCount, 1);

	return SpecHandle;
}

EFirstDefenseResult UFirstBossGameplayAbility::ResolveTargetDefense(AActor* TargetActor,
	const FFirstMeleeDefenseData& AttackData) const
{
	if (!TargetActor)
	{
		return EFirstDefenseResult::Avoided;
	}

	// 只有拥有 DKDefenseComponent 的目标才进入玩家防御系统。
	// 训练假人、其它敌人或未来没有该组件的目标继续走原伤害链。
	if (UDKDefenseComponent* DefenseComponent =
		TargetActor->FindComponentByClass<UDKDefenseComponent>())
	{
		return DefenseComponent->ResolveIncomingMeleeAttack(
			GetAvatarActorFromActorInfo(),
			AttackData);
	}

	return EFirstDefenseResult::Damaged;
}

void UFirstBossGameplayAbility::PlayBossHitPlayerFeedback(const AActor* TargetActor)
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss || !TargetActor)
	{
		return;
	}

	USoundBase* Sound = Boss->GetHitPlayerSound();
	UParticleSystem* VFX = Boss->GetHitPlayerVFX();
	if (!Sound && !VFX)
	{
		return;
	}

	// 躯干位置：按目标胶囊半高的 65% 上移（自动适配不同身高的角色），
	// 避免固定高度导致血花出现在脚底或头顶。
	float TorsoHeightOffset = 90.f;
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		TorsoHeightOffset =
			TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.65f;
	}
	const FVector SpawnLocation =
		TargetActor->GetActorLocation() + FVector(0.f, 0.f, TorsoHeightOffset);

	// 喷溅朝来击方向（BOSS → 玩家）。
	FVector ToTarget = SpawnLocation - Boss->GetActorLocation();
	ToTarget.Z = 0.f;
	const FRotator SpawnRotation = ToTarget.IsNearlyZero()
		? FRotator::ZeroRotator
		: ToTarget.Rotation();

	if (Sound)
	{
		// WorldContextObject 用属于世界的 Boss；音从玩家位置传出。
		UGameplayStatics::PlaySoundAtLocation(
			Boss,
			Sound,
			SpawnLocation,
			FRotator::ZeroRotator);
	}

	if (VFX)
	{
		// 一次性级联粒子：播完自动销毁。
		UGameplayStatics::SpawnEmitterAtLocation(
			Boss,
			VFX,
			SpawnLocation,
			SpawnRotation);
	}
}

void UFirstBossGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
                                              const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	UGameplayEffect* CooldownGE = GetCooldownGameplayEffect();
	if (!CooldownGE)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(
		CooldownGE->GetClass(),
		GetAbilityLevel(Handle, ActorInfo));

	// 冷却标签挂到本次 Effect 上，冷却结束自动移除。
	// GetCooldownTags() 返回 const FGameplayTagContainer*（可能为空），
	// AppendTags 需要引用，所以先判空再解引用。
	if (const FGameplayTagContainer* CooldownTags = GetCooldownTags())
	{
		SpecHandle.Data->DynamicGrantedTags.AppendTags(*CooldownTags);
	}
	SpecHandle.Data->SetSetByCallerMagnitude(
		MyGameplayTags::Boss_SetByCaller_CooldownDuration,
		CooldownDuration);

	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}
