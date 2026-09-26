#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/BOSS/GA_Boss_FiveCombo.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_NormalAttack.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_RetreatChargedSlash.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_ThreeCombo.h"
#include "AI/Tasks/BTTask_BossSelectAttack.h"
#include "DataAssets/StartUpData/DataAsset_StartUpDataBase.h"
#include "MyGameplayTags.h"
#include "UObject/StrongObjectPtr.h"

namespace FirstBossAttackCooldownEditorTests
{
FBossAttackOption MakeOption(const FGameplayTag AbilityTag, const float Duration = -1.f)
{
	FBossAttackOption Option;
	Option.AbilityTag = AbilityTag;
	Option.CooldownDuration = Duration;
	return Option;
}

bool CheckRealStartupSource(FAutomationTestBase& Test, UBTTask_BossSelectAttack& Task)
{
	Test.TestEqual(TEXT("The editor defaults to this project's real BOSS startup data"),
		Task.DefaultCooldownSource.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Enemy/Data/DA_BossStartUpData.DA_BossStartUpData")));
	const UDataAsset_StartUpDataBase* Source = Task.DefaultCooldownSource.LoadSynchronous();
	if (!Test.TestNotNull(TEXT("The real startup data loads without spawning a character"), Source))
	{
		return false;
	}

	// Check the asset's actual grants before using these native CDOs as the
	// independent expected values. No test changes the shared asset or CDOs.
	const TArray<TSubclassOf<UGameplayAbility>> Abilities = Source->GetStartupAbilityClasses();
	bool bValid = Test.TestTrue(TEXT("The startup data grants the normal attack"),
		Abilities.Contains(UGA_Boss_NormalAttack::StaticClass()));
	bValid &= Test.TestTrue(TEXT("The startup data grants the three-combo attack"),
		Abilities.Contains(UGA_Boss_ThreeCombo::StaticClass()));
	bValid &= Test.TestTrue(TEXT("The startup data grants the five-combo attack"),
		Abilities.Contains(UGA_Boss_FiveCombo::StaticClass()));
	bValid &= Test.TestTrue(TEXT("The startup data grants the retreat attack"),
		Abilities.Contains(UGA_Boss_RetreatChargedSlash::StaticClass()));
	return bValid;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossAttackCooldownEditorDefaultsTest,
	"First.Combat.BossAttackCooldown.Editor.RealStartupDefaultsAndManualValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossAttackCooldownEditorDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace FirstBossAttackCooldownEditorTests;
	TStrongObjectPtr<UBTTask_BossSelectAttack> Task(NewObject<UBTTask_BossSelectAttack>());
	if (!CheckRealStartupSource(*this, *Task)) { return false; }
	Task->AttackOptions = {
		MakeOption(MyGameplayTags::Boss_Ability_Attack_Normal),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_ThreeCombo),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_FiveCombo),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_RetreatChargedSlash),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_Normal, 0.f),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_ThreeCombo, 17.25f)
	};

	TestTrue(TEXT("Uninitialized rows receive defaults"), Task->InitializeCooldownDefaults());
	TestEqual(TEXT("Normal attack displays its actual skill default"),
		Task->AttackOptions[0].CooldownDuration, GetDefault<UGA_Boss_NormalAttack>()->CooldownDuration);
	TestEqual(TEXT("Three-combo displays its actual skill default"),
		Task->AttackOptions[1].CooldownDuration, GetDefault<UGA_Boss_ThreeCombo>()->CooldownDuration);
	TestEqual(TEXT("Five-combo displays its actual skill default"),
		Task->AttackOptions[2].CooldownDuration, GetDefault<UGA_Boss_FiveCombo>()->CooldownDuration);
	TestEqual(TEXT("Retreat attack displays its actual skill default"),
		Task->AttackOptions[3].CooldownDuration, GetDefault<UGA_Boss_RetreatChargedSlash>()->CooldownDuration);
	TestEqual(TEXT("An explicitly disabled cooldown stays zero"), Task->AttackOptions[4].CooldownDuration, 0.f);
	TestEqual(TEXT("A manually entered positive cooldown is preserved"), Task->AttackOptions[5].CooldownDuration, 17.25f);
	TestFalse(TEXT("Repeating initialization leaves filled values unchanged"), Task->InitializeCooldownDefaults());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossAttackCooldownEditorChangedRowTest,
	"First.Combat.BossAttackCooldown.Editor.ChangedIdentityRefreshesOnlyItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossAttackCooldownEditorChangedRowTest::RunTest(const FString& Parameters)
{
	using namespace FirstBossAttackCooldownEditorTests;
	TStrongObjectPtr<UBTTask_BossSelectAttack> Task(NewObject<UBTTask_BossSelectAttack>());
	if (!CheckRealStartupSource(*this, *Task)) { return false; }
	Task->AttackOptions = {
		MakeOption(MyGameplayTags::Boss_Ability_Attack_Normal, 13.25f),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_Normal, 0.f),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_ThreeCombo, 23.25f),
		MakeOption(MyGameplayTags::Boss_Ability_Attack_RetreatChargedSlash)
	};
	Task->AttackOptions[0].AbilityTag = MyGameplayTags::Boss_Ability_Attack_FiveCombo;

	TestTrue(TEXT("Changing an identity refreshes that row's displayed default"),
		Task->InitializeCooldownDefaults(0));
	TestEqual(TEXT("The changed row uses the newly chosen skill's default"),
		Task->AttackOptions[0].CooldownDuration, GetDefault<UGA_Boss_FiveCombo>()->CooldownDuration);
	TestEqual(TEXT("The adjacent disabled cooldown is untouched"), Task->AttackOptions[1].CooldownDuration, 0.f);
	TestEqual(TEXT("The adjacent manual cooldown is untouched"), Task->AttackOptions[2].CooldownDuration, 23.25f);
	TestEqual(TEXT("An unrelated pending row is not initialized by a single-row change"),
		Task->AttackOptions[3].CooldownDuration, -1.f);

	TestTrue(TEXT("A later general initialization still fills the pending row"),
		Task->InitializeCooldownDefaults());
	TestEqual(TEXT("The remaining pending row gets its own skill default"),
		Task->AttackOptions[3].CooldownDuration, GetDefault<UGA_Boss_RetreatChargedSlash>()->CooldownDuration);
	TestEqual(TEXT("General initialization continues to preserve manual values"),
		Task->AttackOptions[2].CooldownDuration, 23.25f);
	return true;
}

#endif
