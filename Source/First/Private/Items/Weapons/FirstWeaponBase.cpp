// Fill out your copyright notice in the Description page of Project Settings.


#include "Items/Weapons/FirstWeaponBase.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"

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


