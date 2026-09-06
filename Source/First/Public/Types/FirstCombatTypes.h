#pragma once

#include "CoreMinimal.h"
#include "FirstCombatTypes.generated.h"

// 一次 BOSS 近战命中经过主角防御组件后的唯一结果。
UENUM(BlueprintType)
enum class EFirstDefenseResult : uint8
{
	// 防御系统没有拦截；攻击 Ability 应继续应用生命伤害。
	Damaged,

	// 本击被普通格挡；不应用生命伤害。
	Blocked,

	// 本击被弹反；不应用生命伤害，并取消攻击/削减 BOSS 韧性。
	Parried,

	// 目标死亡或处于无敌帧；本击无需继续结算。
	Avoided
};

// 一次削韧请求的最终结果。
// 普通武器只关心是否请求成功；弹反需要区分 Reduced 与 Broken，
// 以决定是否继续播放 1 秒普通弹反僵直。
UENUM(BlueprintType)
enum class EFirstPoiseDamageResult : uint8
{
	// 参数或状态不合法、目标已死亡/可处决、Poise 已为 0，
	// 或 GE 没有让数值实际下降。
	Ignored,

	// Poise 已实际下降，但仍大于 0。
	Reduced,

	// 本次请求把 Poise 从正数首次降到了 0。
	Broken
};

// 一次近战攻击使用的 Motion Warping 参数。
// 每个 Ability 独立持有一份，方便按武器长度和动画根位移分别调节。
USTRUCT(BlueprintType)
struct FFirstAttackWarpingData
{
	GENERATED_BODY()

	// 超过该水平距离时不修改根位移，只在有效 Warp 窗口内修正朝向。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping", meta=(ClampMin="0.0", Units="cm"))
	float MaxWarpDistance = 300.f;

	// 两个胶囊表面之间额外保留的距离。最终中心距离会自动加上双方胶囊半径。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping", meta=(ClampMin="0.0", Units="cm"))
	float SurfaceGap = 15.f;

	// 单次攻击最多允许 Motion Warping 额外拉近的水平距离，避免接近范围边缘时瞬移过远。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping", meta=(ClampMin="0.0", Units="cm"))
	float MaxWarpTravelDistance = 140.f;

	// true 时，超过 MaxWarpDistance 也保留 AttackTarget，但实际位移仍由
	// MaxWarpTravelDistance 截断。用于超距后冲到最大边界并挥空的承诺型攻击。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping")
	bool bKeepWarpTargetWhenBeyondMaxDistance = false;

	// Motion Warping 通知未处理旋转时使用的平滑转向速度。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping", meta=(ClampMin="0.0", Units="deg/s"))
	float FacingTurnRate = 720.f;

	// 相对攻击起手朝向允许修正的最大角度，防止目标闪到背后时角色瞬间掉头。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping", meta=(ClampMin="0.0", ClampMax="180.0", Units="deg"))
	float MaxFacingAngle = 100.f;

	// true：持续追踪目标；false：BeginAttackWarping 时只计算一次目标 Transform。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping")
	bool bTrackTarget = true;

	// 运行时刷新移动目标和止步点的间隔。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Motion Warping", meta=(ClampMin="0.005", Units="s"))
	float UpdateInterval = 0.02f;
};

// 每种攻击自己描述“能被怎样防御”，组件不依赖具体 Ability 类名。
USTRUCT(BlueprintType)
struct FFirstMeleeDefenseData
{
	GENERATED_BODY()

	// 白名单默认：漏配置的新招式不可格挡。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense")
	bool bBlockable = false;

	// 白名单默认：漏配置的新招式不可弹反。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense")
	bool bParryable = false;

	// 普通格挡成功时扣除的主角精力。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense", meta=(ClampMin="0.0"))
	float GuardStaminaDamage = 0.f;

	// 弹反成功时扣除的 BOSS 韧性。当前普通攻击和三连都填 50。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense", meta=(ClampMin="0.0"))
	float ParryPoiseDamage = 0.f;
};
