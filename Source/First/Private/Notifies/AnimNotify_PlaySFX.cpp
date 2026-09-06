// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/AnimNotify_PlaySFX.h"

#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

void UAnimNotify_PlaySFX::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                 const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// 防御：动画预览器、无 Mesh 的 Actor 都能安全返回（和 ANS_WeaponTelegraph 同一套写法）。
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlaySFX] Skip: no MeshComp"));
		return;
	}
	if (Sounds.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlaySFX] Skip: Sounds is empty | Anim=%s"), Animation ? *Animation->GetName() : TEXT("NULL"));
		return;
	}

	// 每次触发随机挑一个变体；单个时不触发随机（FMath::RandRange(0,0) 恒等）。
	USoundBase* Sound = Sounds[FMath::RandRange(0, Sounds.Num() - 1)];
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlaySFX] Skip: selected Sound is NULL | Anim=%s"), Animation ? *Animation->GetName() : TEXT("NULL"));
		return;
	}

	const float Pitch = FMath::FRandRange(PitchRange.X, PitchRange.Y);

	UE_LOG(LogTemp, Warning, TEXT("[PlaySFX] Triggered: Sound=%s | Volume=%.2f | Pitch=%.2f | Anim=%s | Owner=%s"),
		*Sound->GetName(),
		VolumeMultiplier,
		Pitch,
		Animation ? *Animation->GetName() : TEXT("NULL"),
		MeshComp->GetOwner() ? *MeshComp->GetOwner()->GetName() : TEXT("NULL"));

	// 指定了 Socket（如 BossWeapon_Hand / weapon_socket）就从挂点发声，否则用角色 Mesh 位置。
	// GetSocketLocation 对不存在的 Socket 返回零向量，此时回退 Mesh 位置，避免声音出现在原点。
	FVector Location = MeshComp->GetComponentLocation();
	if (AttachSocketName != NAME_None)
	{
		const FVector SocketLocation = MeshComp->GetSocketLocation(AttachSocketName);
		if (!SocketLocation.IsNearlyZero())
		{
			Location = SocketLocation;
		}
	}

	// PlaySoundAtLocation 使用引擎音频 Actor 池，无需手动管理生命周期。
	// 注意：第一个参数必须传"属于世界的对象"（MeshComp），不能用 this——
	// this 是通知对象，挂在动画资产上，引擎拿不到 World 会静默放弃播放。
	// 引擎原生 AnimNotify_PlaySound 也是传 MeshComp，行为保持一致。
	UGameplayStatics::PlaySoundAtLocation(
		MeshComp,
		Sound,
		Location,
		MeshComp->GetComponentRotation(),
		VolumeMultiplier,
		Pitch,
		0.f,
		AttenuationOverride);
}
