// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/FirstAttributeSet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectExtension.h"
#include "MyGameplayTags.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Components/UI/PawnUIComponent.h"
#include "Interfaces/PawnUIInterface.h"


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
	InitPoise(1.f);
	InitMaxPoise(1.f);
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

	if (Data.EvaluatedData.Attribute == GetPoiseAttribute())
	{
		SetPoise(FMath::Clamp(GetPoise(), 0.f, GetMaxPoise()));
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
		
		AActor* TargetActor = Data.Target.GetAvatarActor();
		const float ActualDamage = OldHealth - NewHealth;
		const FGameplayEffectContextHandle DamageContext =
			Data.EffectSpec.GetContext();
		AActor* DamageInstigator = DamageContext.GetOriginalInstigator();
		if (!DamageInstigator)
		{
			DamageInstigator = DamageContext.GetEffectCauser();
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("DamageTaken %.2f | Target=%s | Instigator=%s | Health %.2f -> %.2f"),
			DamageDone,
			*GetNameSafe(TargetActor),
			*GetNameSafe(DamageInstigator),
			OldHealth,
			NewHealth);

		if (NewHealth <= 0.f)
		{
			if (TargetActor)
			{
				if (UAbilitySystemComponent* TargetASC =
					UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
				{
					// 防止尸体继续受击时把 Loose Tag 计数不断叠加。
					if (!TargetASC->HasMatchingGameplayTag(
						MyGameplayTags::Shared_Status_Dead))
					{
						TargetASC->AddLooseGameplayTag(
							MyGameplayTags::Shared_Status_Dead);
					}
				}
			}
		}
		else if (TargetActor && ActualDamage > 0.f)
		{
			// 只有非致命的真实生命伤害才发送普通受击事件。
			FGameplayEventData EventData;
			EventData.EventTag =
				MyGameplayTags::Shared_Event_DamageReceived;
			EventData.Instigator = DamageInstigator;
			EventData.Target = TargetActor;
			EventData.EventMagnitude = ActualDamage;
			EventData.ContextHandle = DamageContext;

			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				TargetActor,
				MyGameplayTags::Shared_Event_DamageReceived,
				EventData);
		}
		
	}
	
	// —— UI 广播：只在影响这几个属性的 Effect 执行后更新一次 ——
	const FGameplayAttribute& EvaluatedAttribute = Data.EvaluatedData.Attribute;
	if (EvaluatedAttribute == GetHealthAttribute() ||
		EvaluatedAttribute == GetMaxHealthAttribute() ||
		EvaluatedAttribute == GetStaminaAttribute() ||
		EvaluatedAttribute == GetMaxStaminaAttribute() ||
		EvaluatedAttribute == GetPoiseAttribute() ||
		EvaluatedAttribute == GetMaxPoiseAttribute() ||
		EvaluatedAttribute == GetDamageTakenAttribute())
	{
		// 缓存 UI 接口，避免每次执行都 Cast。
		if (!CachedPawnUIInterface.IsValid())
		{
			CachedPawnUIInterface = Cast<IPawnUIInterface>(GetOwningActor());
		}

		if (UPawnUIComponent* UIComponent = CachedPawnUIInterface.IsValid()
			? CachedPawnUIInterface->GetPawnUIComponent()
			: nullptr)
		{
			const float HealthPercent = GetMaxHealth() > 0.f ? GetHealth() / GetMaxHealth() : 0.f;
			const float StaminaPercent = GetMaxStamina() > 0.f ? GetStamina() / GetMaxStamina() : 0.f;
			const float PoisePercent = GetMaxPoise() > 0.f ? GetPoise() / GetMaxPoise() : 0.f;

			UIComponent->OnCurrentHealthChanged.Broadcast(HealthPercent);
			UIComponent->OnCurrentStaminaChanged.Broadcast(StaminaPercent);
			UIComponent->OnCurrentPoiseChanged.Broadcast(PoisePercent);
		}
	}
}
