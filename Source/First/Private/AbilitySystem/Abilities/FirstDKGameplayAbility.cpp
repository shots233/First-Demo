// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/DKCharacter.h"

ADKCharacter* UFirstDKGameplayAbility::GetDKCharacterFromActorInfo()
{
	if (!CachedDKCharacter.IsValid())
	{
		CachedDKCharacter = Cast<ADKCharacter>(CurrentActorInfo->AvatarActor);
	}
	return CachedDKCharacter.Get();
}

UDKCombatComponent* UFirstDKGameplayAbility::GetDKCombatComponentFromActorInfo()
{
	return GetDKCharacterFromActorInfo()? GetDKCharacterFromActorInfo()->GetDKCombatComponent(): nullptr;
}

FGameplayEffectSpecHandle UFirstDKGameplayAbility::MakeDKDamageEffectSpecHandle(
	TSubclassOf<UGameplayEffect> EffectClass, float InWeaponBaseDamage, int32 InComboCount)
{
	// 这里用 check 是为了尽早暴露“攻击 Ability 未配置伤害 GE”的开发期错误。
	// 正式发行版可改为判空并安全结束 Ability。
	
	check(EffectClass);
	
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	check(ASC);
	
	// EffectContext 保存这次伤害来自谁、由哪个 Ability 发起；
	// 受击特效、命中方向、日志等扩展都可从这里读取来源信息。
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.SetAbility(this);
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());
	
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, GetAbilityLevel(), ContextHandle);
	
	// 不把基础伤害写死在 GE 中：同一个 UFirstGE_Damage 就能服务不同武器。
	SpecHandle.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_BaseDamage, InWeaponBaseDamage);
	
	// 连击段数同样作为本次 Spec 的临时数据传给伤害计算公式。
	SpecHandle.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_ComboCount, InComboCount);
	
	return SpecHandle;
	
}
