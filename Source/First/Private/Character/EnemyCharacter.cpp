// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/EnemyCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"

AEnemyCharacter::AEnemyCharacter()
{
	ConfigureCameraCollision();

	TargetLockPoint =CreateDefaultSubobject<USceneComponent>(TEXT("TargetLockPoint"));
	TargetLockPoint->SetupAttachment(GetRootComponent());

	// ACharacter 的 ActorLocation 已在胶囊中心附近；
	// 再向上 40，首版通常会落在上半身。
	TargetLockPoint->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
}

void AEnemyCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// 已保存的蓝图/关卡实例可能覆盖构造默认值，初始化完成后统一应用相机规则。
	ConfigureCameraCollision();
}

void AEnemyCharacter::ConfigureCameraCollision()
{
	// 单独修改 Camera；不替换整套碰撞预设，也不关闭胶囊或网格体碰撞。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

FVector AEnemyCharacter::GetTargetLockLocation() const
{
	return TargetLockPoint? TargetLockPoint->GetComponentLocation(): GetActorLocation() + FVector(0.f, 0.f, 40.f);
}
