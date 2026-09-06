// Fill out your copyright notice in the Description page of Project Settings.


#include "Items/Weapons/FirstWeaponBase.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MyGameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

// Sets default values
AFirstWeaponBase::AFirstWeaponBase()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("WeaponCollisionBox"));
	WeaponCollisionBox->SetupAttachment(GetRootComponent());
	
	WeaponCollisionBox->SetBoxExtent(FVector(12.f, 12.f, 50.f));
	WeaponCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponCollisionBox->SetCollisionObjectType(ECC_WorldDynamic);
	WeaponCollisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	WeaponCollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	WeaponCollisionBox->SetGenerateOverlapEvents(true);
	
	WeaponCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this,&ThisClass::OnCollisionBoxBeginOverlap);

	WeaponCollisionBox->OnComponentEndOverlap.AddUniqueDynamic(this,&ThisClass::OnCollisionBoxEndOverlap);
	
	// 拖影初始为关闭；只有蒙太奇的 Slash Trail 通知按挥剑窗口开启。
	SlashTrailComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SlashTrailComponent"));
	SlashTrailComponent->SetupAttachment(WeaponMesh);
	SlashTrailComponent->SetAutoActivate(false);
	SlashTrailComponent->SetAutoDestroy(false);

	// 白光预警拥有独立组件，避免与挥砍拖影的开关和资源互相覆盖。
	WeaponTelegraphComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("WeaponTelegraphComponent"));
	WeaponTelegraphComponent->SetupAttachment(WeaponMesh);
	WeaponTelegraphComponent->SetAutoActivate(false);
	WeaponTelegraphComponent->SetAutoDestroy(false);

}

bool AFirstWeaponBase::IsTargetDead(const AActor* Target)
{
	if (!Target)
	{
		return false;
	}

	// 与 ABaseCharacter::IsAttackWarpTargetUsable 同口径：只把明确带
	// Shared_Status_Dead 标签的目标视为死亡，没有 ASC 的目标不作拦截。
	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Target));
	return TargetASC && TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);
}

void AFirstWeaponBase::OnCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// SpawnSword 后必须 SetInstigator(DKCharacter)，否则无法排除自己，也找不到攻击者。
	const APawn* WeaponOwningPawn = GetInstigator();
	if (!WeaponOwningPawn || !OtherActor || WeaponOwningPawn == OtherActor)
	{
		return;
	}

	// 本阶段只把 Pawn 作为可受击目标；可破坏箱子等物体可在后续改为接口或 Object Channel 判断。
	if (Cast<APawn>(OtherActor))
	{
		// 命中链第一道门：尸体不进入任何结算，也不播命中音效/血花。
		if (IsTargetDead(OtherActor))
		{
			return;
		}

		OnWeaponHitTarget.ExecuteIfBound(OtherActor);
	}
}

void AFirstWeaponBase::OnCollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	const APawn* WeaponOwningPawn = GetInstigator();
	if (!WeaponOwningPawn || !OtherActor || WeaponOwningPawn == OtherActor)
	{
		return;
	}
	
	if (Cast<APawn>(OtherActor))
	{
		OnWeaponPulledFromTarget.ExecuteIfBound(OtherActor);
	}
}

void AFirstWeaponBase::SetSlashTrailEnabled(bool bShouldEnable)
{
	if (!SlashTrailComponent)
	{
		return;
	}

	if (bShouldEnable)
	{
		// 模板存在且与当前资源不同时才替换，避免每次挥剑重复 SetAsset。
		if (SlashTrailTemplate && SlashTrailComponent->GetAsset() != SlashTrailTemplate)
		{
			SlashTrailComponent->SetAsset(SlashTrailTemplate);
		}

		SlashTrailComponent->Activate(true);
	}
	else
	{
		SlashTrailComponent->Deactivate();
	}
}

void AFirstWeaponBase::SetWeaponTelegraphEnabled(bool bShouldEnable)
{
	// 关闭路径始终强制执行，确保 Notify End 与 Ability 兜底可以安全重复调用。
	if (!bShouldEnable)
	{
		bWeaponTelegraphActive = false;
		if (WeaponTelegraphComponent)
		{
			WeaponTelegraphComponent->Deactivate();
		}
		return;
	}

	// 同一预警窗口重复开启时不重启 Niagara，也不重复播放提示音。
	if (bWeaponTelegraphActive)
	{
		return;
	}

	// 没有预警模板时保持关闭；玩家武器无需配置该资源，也不会误播 BOSS 提示音。
	if (!WeaponTelegraphComponent || !WeaponTelegraphTemplate)
	{
		bWeaponTelegraphActive = false;
		if (WeaponTelegraphComponent)
		{
			WeaponTelegraphComponent->Deactivate();
		}
		return;
	}

	if (WeaponTelegraphComponent->GetAsset() != WeaponTelegraphTemplate)
	{
		WeaponTelegraphComponent->SetAsset(WeaponTelegraphTemplate);
	}

	bWeaponTelegraphActive = true;
	WeaponTelegraphComponent->Activate(true);

	if (WeaponTelegraphSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			WeaponTelegraphSound,
			WeaponTelegraphComponent->GetComponentLocation());
	}
}
