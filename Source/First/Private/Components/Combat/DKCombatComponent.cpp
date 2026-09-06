// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Combat/DKCombatComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Items/Weapons/DKWeapon.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "Character/BossCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "MyGameplayTags.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

ADKWeapon* UDKCombatComponent::GetDKCarriedWeaponByTag(FGameplayTag InWeaponTag) const
{
	return Cast<ADKWeapon>(GetCharacterCarriedWeaponByTag(InWeaponTag));
}

ADKWeapon* UDKCombatComponent::GetDKCurrentEquippedWeapon() const
{
	return Cast<ADKWeapon>(GetCharacterCurrentEquippedWeapon());
}

float UDKCombatComponent::GetDKCurrentEquippedWeaponDamageAtLevel(float InLevel) const
{
	const ADKWeapon* CurrentWeapon = GetDKCurrentEquippedWeapon();
	return CurrentWeapon? CurrentWeapon->DKWeaponData.WeaponBaseDamage.GetValueAtLevel(InLevel): 0.f;
}

float UDKCombatComponent::
	GetDKCurrentEquippedWeaponPoiseDamageAtLevel(
		float InLevel) const
{
	const ADKWeapon* CurrentWeapon =
		GetDKCurrentEquippedWeapon();

	const float PoiseDamage = CurrentWeapon
		? CurrentWeapon->DKWeaponData.WeaponPoiseDamage
			.GetValueAtLevel(InLevel)
		: 0.f;

	// 即使以后蓝图或曲线配置错误，也不允许返回负削韧。
	return FMath::Max(PoiseDamage, 0.f);
}

void UDKCombatComponent::OnHitTargetActor(AActor* HitActor)
{
	Super::OnHitTargetActor(HitActor);
	// 同一碰撞窗口内同一目标只发一次事件，避免 BeginOverlap 抖动或多个组件重复扣血。
	// 第二道门：死亡目标不发命中事件，也不播表现（武器入口已过滤，这里兜住其它调用路径）。
	if (!HitActor || OverlappedActors.Contains(HitActor) ||
		AFirstWeaponBase::IsTargetDead(HitActor))
	{
		return;
	}
	
	//将命中目标添加进数组避免重复伤害同一目标。
	OverlappedActors.AddUnique(HitActor);
	
	// 事件里只传递“谁打中了谁”；伤害值由监听事件的攻击 Ability 根据当前武器和连击段数决定。
	FGameplayEventData EventData;
	EventData.Instigator = GetOwningPawn();
	EventData.Target = HitActor;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(GetOwningPawn(),MyGameplayTags::DK_Event_MeleeHit,EventData);

	// 结果音在事件结算前播放：命中即“刀砍到目标”，从被击目标位置发出。
	PlayHitTargetSFX(HitActor);
	PlayHitTargetVFX(HitActor);
}

void UDKCombatComponent::PlayHitTargetSFX(AActor* HitActor) const
{
	if (!HitActor || !HitSound)
	{
		return;
	}

	// 命中音从被击目标位置发出（剑砍到 BOSS 的声音从 BOSS 处传向玩家）。
	// WorldContextObject 用属于世界的 HitActor，与防御组件同款写法。
	UGameplayStatics::PlaySoundAtLocation(
		HitActor,
		HitSound,
		HitActor->GetActorLocation(),
		FRotator::ZeroRotator);
}

void UDKCombatComponent::PlayHitTargetVFX(AActor* HitActor) const
{
	const APawn* InstigatorPawn = GetOwningPawn();
	if (!HitActor || !InstigatorPawn)
	{
		return;
	}

	// 血花按受击者组织：不同敌人/BOSS 配自己的血（BP 上 Boss|Feedback → Blood VFX）。
	// 当前只有 ABossCharacter 会溅血；未来普通敌人同样加一个 BloodVFX 属性即可。
	UParticleSystem* BloodVFX = nullptr;
	if (const ABossCharacter* BossTarget = Cast<ABossCharacter>(HitActor))
	{
		BloodVFX = BossTarget->GetBloodVFX();
	}
	if (!BloodVFX)
	{
		return;
	}

	// 躯干位置：按目标胶囊半高的 65% 上移（自动适配不同身高的角色），
	// 避免固定高度导致血花出现在脚底或头顶。
	float TorsoHeightOffset = 90.f;
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(HitActor))
	{
		TorsoHeightOffset =
			TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.65f;
	}
	const FVector SpawnLocation =
		HitActor->GetActorLocation() + FVector(0.f, 0.f, TorsoHeightOffset);

	// 让喷溅朝挥刀方向（玩家 → 目标的水平朝向）。
	FVector ToTarget = HitActor->GetActorLocation() - InstigatorPawn->GetActorLocation();
	ToTarget.Z = 0.f;
	const FRotator SpawnRotation = ToTarget.IsNearlyZero()
		? FRotator::ZeroRotator
		: ToTarget.Rotation();

	// 一次性级联粒子：播完自动销毁，无需管理生命周期。
	UGameplayStatics::SpawnEmitterAtLocation(
		HitActor,
		BloodVFX,
		SpawnLocation,
		SpawnRotation);
}
