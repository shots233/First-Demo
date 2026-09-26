// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/FirstPlayerHUDWidget.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/DKCharacter.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/UI/DKUIComponent.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "Styling/SlateBrush.h"
#include "TimerManager.h"

void UFirstPlayerHUDWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyStaminaExhaustedBorderStyle();

	// 设计器中始终显示描边以便摆放；运行时读取真实的闪避恢复锁。
	const UFirstAbilitySystemComponent* ASC = BoundASC.Get();
	SetStaminaExhaustedBorderVisible(IsDesignTime() ||
		(ASC && ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_DodgeExhausted)));
}

void UFirstPlayerHUDWidget::NativeConstruct()
{
	UnbindFromPlayer();
	bIsConstructed = true;
	Super::NativeConstruct();

	if (bIsConstructed && !IsDesignTime())
	{
		RetryBindPlayer();
	}
}

void UFirstPlayerHUDWidget::NativeDestruct()
{
	bIsConstructed = false;
	UnbindFromPlayer();
	Super::NativeDestruct();
}

void UFirstPlayerHUDWidget::BindToPlayer(ADKCharacter* InPlayer)
{
	UnbindFromPlayer();
	if (!bIsConstructed || !InPlayer)
	{
		return;
	}

	if (UDKUIComponent* DKUI = InPlayer->GetDKUIComponent())
	{
		BoundDKUI = DKUI;
		DKUI->OnCurrentHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleHealthChanged);
		DKUI->OnCurrentStaminaChanged.AddUniqueDynamic(this, &ThisClass::HandleStaminaChanged);
	}

	if (UFirstAbilitySystemComponent* ASC = InPlayer->GetFirstAbilitySystemComponent())
	{
		BoundASC = ASC;
		// 扣费先广播精力，再设置恢复锁；描边必须独立监听 Tag，不能只跟随百分比更新。
		DodgeExhaustedChangedHandle = ASC->RegisterGameplayTagEvent(
			MyGameplayTags::DK_Status_DodgeExhausted, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleDodgeExhaustedChanged);
		SetStaminaExhaustedBorderVisible(ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_DodgeExhausted));
	}

	// 出生时 GE 已经广播过一次（当时 HUD 还没创建），这里补读当前值。
	const UFirstAttributeSet* AttrSet = InPlayer->GetFirstAttributeSet();
	if (AttrSet)
	{
		HandleHealthChanged(AttrSet->GetMaxHealth() > 0.f ? AttrSet->GetHealth() / AttrSet->GetMaxHealth() : 0.f);
		HandleStaminaChanged(AttrSet->GetMaxStamina() > 0.f ? AttrSet->GetStamina() / AttrSet->GetMaxStamina() : 0.f);
	}
}

void UFirstPlayerHUDWidget::UnbindFromPlayer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryBindPlayerTimerHandle);
	}
	RetryBindPlayerTimerHandle.Invalidate();

	if (UDKUIComponent* DKUI = BoundDKUI.Get())
	{
		DKUI->OnCurrentHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
		DKUI->OnCurrentStaminaChanged.RemoveDynamic(this, &ThisClass::HandleStaminaChanged);
	}
	if (UFirstAbilitySystemComponent* ASC = BoundASC.Get(); ASC && DodgeExhaustedChangedHandle.IsValid())
	{
		ASC->RegisterGameplayTagEvent(MyGameplayTags::DK_Status_DodgeExhausted,
			EGameplayTagEventType::NewOrRemoved).Remove(DodgeExhaustedChangedHandle);
	}
	DodgeExhaustedChangedHandle.Reset();
	BoundDKUI.Reset();
	BoundASC.Reset();
	SetStaminaExhaustedBorderVisible(false);
}

void UFirstPlayerHUDWidget::HandleDodgeExhaustedChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (bIsConstructed)
	{
		SetStaminaExhaustedBorderVisible(NewCount > 0);
	}
}

void UFirstPlayerHUDWidget::ApplyStaminaExhaustedBorderStyle()
{
	if (!StaminaExhaustedBorder)
	{
		return;
	}

	FSlateBrush BorderBrush;
	BorderBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	BorderBrush.TintColor = FSlateColor(FLinearColor::Transparent);
	BorderBrush.ImageSize = FVector2D::ZeroVector;
	BorderBrush.OutlineSettings.Color = FSlateColor(StaminaExhaustedBorderColor);
	BorderBrush.OutlineSettings.Width = FMath::Max(StaminaExhaustedBorderThickness, 0.f);
	BorderBrush.OutlineSettings.CornerRadii = FVector4(0.f, 0.f, 0.f, 0.f);
	BorderBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	BorderBrush.OutlineSettings.bUseBrushTransparency = false;
	StaminaExhaustedBorder->SetBrush(BorderBrush);
	StaminaExhaustedBorder->SetBrushColor(FLinearColor::White);
	StaminaExhaustedBorder->SetPadding(FMargin(0.f));
}

void UFirstPlayerHUDWidget::SetStaminaExhaustedBorderVisible(bool bVisible)
{
	if (StaminaExhaustedBorder)
	{
		StaminaExhaustedBorder->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UFirstPlayerHUDWidget::HandleHealthChanged(float NewPercent)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(NewPercent);
	}
}

void UFirstPlayerHUDWidget::HandleStaminaChanged(float NewPercent)
{
	if (StaminaBar)
	{
		StaminaBar->SetPercent(NewPercent);
	}
}

void UFirstPlayerHUDWidget::RetryBindPlayer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryBindPlayerTimerHandle);
	}
	// Construct 可能先于加入视口；用自身生命周期判断，避免首次绑定永远漏掉。
	if (!bIsConstructed)
	{
		return;
	}

	ADKCharacter* Player = Cast<ADKCharacter>(GetOwningPlayerPawn());
	if (Player)
	{
		BindToPlayer(Player);
	}
	else if (UWorld* World = GetWorld())
	{
		RetryBindPlayerTimerHandle = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RetryBindPlayer);
	}
}
