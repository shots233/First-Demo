// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Combat/DKCombatComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Items/Weapons/DKWeapon.h"
#include "MyGameplayTags.h"

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

void UDKCombatComponent::OnHitTargetActor(AActor* HitActor)
{
	Super::OnHitTargetActor(HitActor);
	// 同一碰撞窗口内同一目标只发一次事件，避免 BeginOverlap 抖动或多个组件重复扣血。
	if (!HitActor || OverlappedActors.Contains(HitActor))
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
	
}
