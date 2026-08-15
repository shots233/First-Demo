// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Combat/FirstCombatComponent.h"

#include "Components/BoxComponent.h"
#include "Items/Weapons/FirstWeaponBase.h"

// Sets default values for this component's properties
UFirstCombatComponent::UFirstCombatComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	
}

void UFirstCombatComponent::RegisterSpawnedWeapon(FGameplayTag InWeaponTagToRegister,
	AFirstWeaponBase* InWeaponToRegister, bool bRegisterAsEquippedWeapon)
{
	check(InWeaponTagToRegister.IsValid());
	check(InWeaponToRegister);
	checkf(!CharacterCarriedWeaponMap.Contains(InWeaponTagToRegister),TEXT("Weapon tag already registered: %s"),*InWeaponTagToRegister.ToString());
	
	// Tag 是武器库存的键；当前项目只有剑，但 Map 让以后添加盾、双手剑时无需改接口。
	CharacterCarriedWeaponMap.Emplace(InWeaponTagToRegister, InWeaponToRegister);
	
	// 武器只报告碰撞，组件决定如何处理。BindUObject 会在组件失效时自动避免调用悬空对象。
	InWeaponToRegister->OnWeaponHitTarget.BindUObject(this, &ThisClass::OnHitTargetActor);
	InWeaponToRegister->OnWeaponPulledFromTarget.BindUObject(this, &ThisClass::OnWeaponPulledFromTargetActor);

	if (bRegisterAsEquippedWeapon)
	{
		CurrentEquippedWeaponTag = InWeaponTagToRegister;
	}
}

AFirstWeaponBase* UFirstCombatComponent::GetCharacterCarriedWeaponByTag(FGameplayTag InWeaponTagToGet) const
{
	if (const TObjectPtr<AFirstWeaponBase>* FoundWeapon = CharacterCarriedWeaponMap.Find(InWeaponTagToGet))
	{
		return FoundWeapon->Get();
	}
	return nullptr;
}

void UFirstCombatComponent::ToggleWeaponCollision(bool bShouldEnable)
{
	AFirstWeaponBase* WeaponToToggle = GetCharacterCurrentEquippedWeapon();
	if (!WeaponToToggle)
	{
		return;
	}

	// QueryOnly 足以产生 Overlap，不让剑盒参与物理推挤；攻击动画应由角色网格驱动。
	WeaponToToggle->GetWeaponCollisionBox()->SetCollisionEnabled(bShouldEnable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	
	if (!bShouldEnable)
	{
		// 一次挥砍结束，清除“已经命中过谁”的名单；下一次挥砍可以再次命中同一敌人。
		OverlappedActors.Empty();
	}
}

AFirstWeaponBase* UFirstCombatComponent::GetCharacterCurrentEquippedWeapon() const
{
	if (!CurrentEquippedWeaponTag.IsValid())
	{
		return nullptr;
	}
	
	return GetCharacterCarriedWeaponByTag(CurrentEquippedWeaponTag);
}

void UFirstCombatComponent::OnHitTargetActor(AActor* HitActor)
{

}

void UFirstCombatComponent::OnWeaponPulledFromTargetActor(AActor* InteractedActor)
{
}

APawn* UFirstCombatComponent::GetOwningPawn() const
{
	return Cast<APawn>(GetOwner());
}





