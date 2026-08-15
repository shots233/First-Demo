// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/FirstAttributeSet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectExtension.h"
#include "MyGameplayTags.h"


UFirstAttributeSet::UFirstAttributeSet()
{
	// 这些是“未应用初始 GE 前”的安全默认值。真正游戏数值由 GE_DK_Initialize 覆盖，
	InitHealth(1.f);
	InitMaxHealth(1.f);
	InitStamina(1.f);
	InitMaxStamina(1.f);
	InitAttackPower(1.f);
	InitDefensePower(1.f);
	InitDamageTaken(0.f);
}

void UFirstAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		// 任何 Effect 都可能修改 Health,并且限制health的值范围。
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	
	if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));
	}

	if (Data.EvaluatedData.Attribute == GetDamageTakenAttribute())
	{
		// 先缓存再归零，防止这次伤害在下一次 GE 执行时被重复使用。
		const float DamageDone = GetDamageTaken();
		SetDamageTaken(0.f);
		
		if (DamageDone <= 0.f)
		{
			return;
		}
		
		// 伤害结算只在 AttributeSet 内改变生命，攻击 Ability 不直接 SetHealth，
		// 这样敌人的攻击、陷阱伤害也能复用同一条扣血与死亡判断路径。
		const float OldHealth = GetHealth();
		const float NewHealth = FMath::Clamp(OldHealth - DamageDone, 0.f, GetMaxHealth());
		SetHealth(NewHealth);
		
		//检查
		UE_LOG(LogTemp, Warning,TEXT("DamageTaken %.2f | Health %.2f -> %.2f"),DamageDone,OldHealth,NewHealth);
		
		if (NewHealth <= 0.f)
		{
			if (AActor* TargetActor = Data.Target.GetAvatarActor())
			{
				if (UAbilitySystemComponent* TargetASC =UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
				{
					// 防止尸体后续再次受击时把 Loose Tag 计数不断叠加。
					if (!TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
					{
						// 第二阶段先用死亡 Tag 阻止新 Ability；死亡动画/销毁可留到后续扩展。
						TargetASC->AddLooseGameplayTag(MyGameplayTags::Shared_Status_Dead);
					}
				}
			}
		}
		
	}
	
}
