// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/EnemyCharacter.h"

#include "Components/SceneComponent.h"

AEnemyCharacter::AEnemyCharacter()
{
	TargetLockPoint =CreateDefaultSubobject<USceneComponent>(TEXT("TargetLockPoint"));
	TargetLockPoint->SetupAttachment(GetRootComponent());

	// ACharacter 的 ActorLocation 已在胶囊中心附近；
	// 再向上 40，首版通常会落在上半身。
	TargetLockPoint->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
}

FVector AEnemyCharacter::GetTargetLockLocation() const
{
	return TargetLockPoint? TargetLockPoint->GetComponentLocation(): GetActorLocation() + FVector(0.f, 0.f, 40.f);
}
