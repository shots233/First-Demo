// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/DK//FirstGA_DKSpawnSword.h"

#include "MyGameplayTags.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Items/Weapons/DKWeapon.h"

UFirstGA_DKSpawnSword::UFirstGA_DKSpawnSword()
{
	//Ability 为每个拥有它的 Actor 保留一个独立实例。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	//这个 Ability 被 GiveAbility() 授予 ASC 后，立刻尝试激活。
	AbilityActivationPolicy = EFirstAbilityActivationPolicy::OnGiven;

	//创建一个局部 GameplayTag 容器
	FGameplayTagContainer AssetTags;
	//加入身份 Tag
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Spawn_Sword);
	//将刚才构建的 Tag 容器设置为这个 Ability 的默认身份标签。
	SetAssetTags(AssetTags);
}

void UFirstGA_DKSpawnSword::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	ADKCharacter* DKCharacter = GetDKCharacterFromActorInfo();
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	if (!DKCharacter || !CombatComponent)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	
	// OnRep/重复初始化等意外路径不应生成第二把同 Tag 武器。
	if (CombatComponent->GetDKCarriedWeaponByTag(MyGameplayTags::DK_Weapon_Sword))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	const TSubclassOf<ADKWeapon> WeaponClass = DKCharacter->GetDefaultWeaponClass();
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s] DefaultWeaponClass is not configured"),*GetNameSafe(DKCharacter));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = DKCharacter;
	SpawnParameters.Instigator = DKCharacter;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	ADKWeapon* SpawnedWeapon = DKCharacter->GetWorld()->SpawnActor<ADKWeapon>(WeaponClass, DKCharacter->GetActorTransform(), SpawnParameters);
	if (!SpawnedWeapon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	
	const FName BackSocket = SpawnedWeapon->DKWeaponData.UnequippedSocketName;
	if (!DKCharacter->GetMesh()->DoesSocketExist(BackSocket) || 
		!SpawnedWeapon->AttachToComponent(DKCharacter->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, BackSocket))
	{
		UE_LOG(LogTemp, Error, TEXT("Sword back socket is invalid: %s"), *BackSocket.ToString());
		SpawnedWeapon->Destroy();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	
	// false 表示“已携带但未装备”。剑已经存在，CurrentEquippedWeaponTag 仍保持为空。
	CombatComponent->RegisterSpawnedWeapon(MyGameplayTags::DK_Weapon_Sword,SpawnedWeapon,false);
	
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
