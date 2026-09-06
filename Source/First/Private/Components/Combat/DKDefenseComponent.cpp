#include "Components/Combat/DKDefenseComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenPause.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "MyGameplayTags.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

UDKDefenseComponent::UDKDefenseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UDKDefenseComponent::IsAttackerInsideGuardArc(const AActor* Attacker) const
{
	const AActor* Defender = GetOwner();
	if (!Defender || !Attacker)
	{
		return false;
	}

	FVector Forward = Defender->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();

	FVector ToAttacker =Attacker->GetActorLocation() - Defender->GetActorLocation();
	ToAttacker.Z = 0.f;

	// 两个 Actor 几乎重合时无法得到稳定方向；按正面处理更安全。
	if (ToAttacker.IsNearlyZero())
	{
		return true;
	}

	ToAttacker.Normalize();

	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(GuardHalfAngleDegrees));

	return FVector::DotProduct(Forward, ToAttacker) >= MinimumDot;
}

void UDKDefenseComponent::ApplyStaminaDelta(
	UAbilitySystemComponent* ASC,
	AActor* Attacker,
	float Delta) const
{
	if (!ASC || FMath::IsNearlyZero(Delta))
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Attacker);
	Context.AddInstigator(Attacker, Attacker);

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UFirstGE_StaminaChange::StaticClass(),1.f,Context);

	if (!Spec.IsValid())
	{
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(MyGameplayTags::Combat_SetByCaller_StaminaDelta,Delta);

	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UDKDefenseComponent::ApplyStaminaRegenPause(
	UAbilitySystemComponent* ASC,
	AActor* Attacker) const
{
	if (!ASC)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Attacker);
	Context.AddInstigator(Attacker, Attacker);

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UFirstGE_StaminaRegenPause::StaticClass(),1.f,Context);

	if (Spec.IsValid())
	{
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

EFirstDefenseResult UDKDefenseComponent::ResolveIncomingMeleeAttack(
	AActor* Attacker,
	const FFirstMeleeDefenseData& AttackData)
{
	AActor* Defender = GetOwner();
	UAbilitySystemComponent* ASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Defender);

	if (!Defender || !Attacker || !ASC)
	{
		return EFirstDefenseResult::Damaged;
	}

	// 死亡和闪避无敌优先于防御；不播放格挡/弹反表现。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Invincible))
	{
		return EFirstDefenseResult::Avoided;
	}

	if (!IsAttackerInsideGuardArc(Attacker))
	{
		// BOSS 命中反馈（音效+血花）已由 BOSS GA 在判定 Damaged 后播放，玩家侧不再重复。
		return EFirstDefenseResult::Damaged;
	}

	// 弹反优先于 Blocking。按下前 0.15 秒两个标签会同时存在。
	if (AttackData.bParryable &&ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_ParryWindow))
	{
		FGameplayEventData PlayerEvent;
		PlayerEvent.Instigator = Attacker;
		PlayerEvent.Target = Defender;
		PlayerEvent.EventMagnitude = AttackData.ParryPoiseDamage;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Defender,MyGameplayTags::DK_Event_ParrySuccess,PlayerEvent);

		FGameplayEventData BossEvent;
		BossEvent.Instigator = Defender;
		BossEvent.Target = Attacker;
		BossEvent.EventMagnitude = AttackData.ParryPoiseDamage;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Attacker,MyGameplayTags::Boss_Event_Parried,BossEvent);

		PlayDefenseSFX(ParrySuccessSound);
		PlayDefenseVFX(ParrySuccessVFX, Attacker);
		PlayDefenseShake(ParryShakeClass, ParryShakeScale);

		return EFirstDefenseResult::Parried;
	}

	const float OldStamina = ASC->GetNumericAttribute(UFirstAttributeSet::GetStaminaAttribute());

	if (AttackData.bBlockable && OldStamina > 0.f && ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Blocking))
	{
		const float StaminaDamage = FMath::Max(AttackData.GuardStaminaDamage, 0.f);

		ApplyStaminaDelta(ASC, Attacker, -StaminaDamage);
		ApplyStaminaRegenPause(ASC, Attacker);

		const float NewStamina = ASC->GetNumericAttribute(UFirstAttributeSet::GetStaminaAttribute());

		FGameplayEventData BlockEvent;
		BlockEvent.Instigator = Attacker;
		BlockEvent.Target = Defender;
		BlockEvent.EventMagnitude = StaminaDamage;

		if (NewStamina <= 0.f)
		{
			// 这一击仍返回 Blocked；破防 Ability 会立刻移除 Blocking，
			// 所以后续三连段数才能正常扣血。
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Defender,MyGameplayTags::DK_Event_GuardBroken,BlockEvent);
			PlayDefenseSFX(GuardBrokenSound);
			PlayDefenseVFX(GuardBrokenVFX, Attacker);
		}
		else
		{
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Defender,MyGameplayTags::DK_Event_GuardHit,BlockEvent);
			PlayDefenseSFX(GuardHitSound);
			PlayDefenseVFX(GuardHitVFX, Attacker);
			PlayDefenseShake(GuardHitShakeClass, GuardHitShakeScale);
		}

		return EFirstDefenseResult::Blocked;
	}

	// BOSS 命中反馈（音效+血花）已由 BOSS GA 在判定 Damaged 后播放，玩家侧不再重复。
	return EFirstDefenseResult::Damaged;
}

void UDKDefenseComponent::PlayDefenseVFX(UParticleSystem* VFX, const AActor* Attacker) const
{
	AActor* Defender = GetOwner();
	if (!Defender || !VFX || !Attacker)
	{
		return;
	}

	// 躯干位置：按目标胶囊半高的 65% 上移（自动适配不同身高的角色），
	// 避免固定高度导致火花出现在脚底或头顶。
	float TorsoHeightOffset = 90.f;
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(Defender))
	{
		TorsoHeightOffset =
			TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.65f;
	}
	const FVector SpawnLocation =
		Defender->GetActorLocation() + FVector(0.f, 0.f, TorsoHeightOffset);

	// 让喷溅朝来击方向（Attacker → Defender 的水平朝向），打击感更强。
	FVector ToDefender = Defender->GetActorLocation() - Attacker->GetActorLocation();
	ToDefender.Z = 0.f;
	const FRotator SpawnRotation = ToDefender.IsNearlyZero()
		? FRotator::ZeroRotator
		: ToDefender.Rotation();

	// 一次性级联粒子：播完自动销毁，无需管理生命周期。
	// WorldContextObject 用属于世界的 Defender，与音效同款写法。
	UGameplayStatics::SpawnEmitterAtLocation(
		Defender,
		VFX,
		SpawnLocation,
		SpawnRotation);
}

void UDKDefenseComponent::PlayDefenseShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale) const
{
	if (!ShakeClass)
	{
		return;
	}

	// 弹反/格挡是玩家视角反馈：直接在玩家自己的 PlayerCameraManager 上播放，
	// 不随距离衰减（观察者本身就是玩家）。单机 PIE 下 PlayerIndex=0。
	if (APlayerCameraManager* CameraManager =
		UGameplayStatics::GetPlayerCameraManager(GetOwner(), 0))
	{
		CameraManager->StartCameraShake(ShakeClass, Scale);
	}
}

void UDKDefenseComponent::PlayDefenseSFX(USoundBase* Sound) const
{
	AActor* Defender = GetOwner();
	if (!Defender || !Sound)
	{
		return;
	}

	// 结果音从"被打/格挡/弹反"的玩家位置发出 3D 空间化音效；
	// WorldContextObject 用 GetOwner()（属于世界），与引擎原生通知的行为一致。
	UGameplayStatics::PlaySoundAtLocation(
		Defender,
		Sound,
		Defender->GetActorLocation(),
		FRotator::ZeroRotator);
}