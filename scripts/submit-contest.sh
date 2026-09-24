#!/usr/bin/env bash
# 一键整理并提交专属仓（需本机可写 contest 目录 + GitHub 登录）
set -euo pipefail
SRC=/home/ubuntu/jianwei-id
DST=/home/ubuntu/contest2026_159_weishizhixing_work
SUB=/home/ubuntu/contest2026_159_weishizhixing_submission

echo "=== 1. 同步最新代码到专属仓工作区 ==="
cp -f "$SRC/app/jianwei/jianwei_main.c" "$SRC/app/jianwei/jianwei_ui.c" \
      "$SRC/app/jianwei/defconfig.append" "$SRC/app/jianwei/jianwei.h" \
      "$DST/app/jianwei/"
cp -f "$SRC/cloud/app.py" "$SRC/cloud/.env.example" "$DST/cloud/"
cp -f "$SRC/cloud/local_recog.py" "$DST/cloud/" 2>/dev/null || true
cp -a "$SRC/cloud/templates/." "$DST/cloud/templates/"
mkdir -p "$DST/scripts"
cp -f "$SRC/scripts/"*.sh "$SRC/scripts/"*.bat "$DST/scripts/" 2>/dev/null || true
cp -f "$SRC/skills/jianwei-antifraud/SKILL.md" "$DST/skills/jianwei-antifraud/"
rm -f "$DST"/app/jianwei/*.o "$DST"/app/jianwei/Make.dep "$DST"/app/jianwei/.built 2>/dev/null || true
rm -f "$DST/cloud/.env" 2>/dev/null || true
rm -rf "$DST/cloud/skills" 2>/dev/null || true

cat > "$DST/docs/SUBMIT_NEXT.md" << 'EOF'
# 提交说明（队 159 / weishizhixing）

## 本包已含
- `app/jianwei/`：openvela 板端（BOOT、fb0、心跳；`scripts/one-shot-bringup.sh` 含开机自启）
- `cloud/` + `skills/jianwei-antifraud/`：模式 A，OCR/ASR + Skill+LLM 风险判定（`/api/ai-status`）
- `firmware/`：Arduino 回退
- `docs/见微随身证_openvela作品提交.docx`

## 官网仍须人工
1. 签 CLA，官网交视频 + 仓链接 + docx
2. 录 ≤5 分钟演示（含 `nsh> jianwei`）
3. AI Coding 日志放入 `logs/`
EOF

echo "=== 2. git 提交 ==="
cd "$DST"
git add -A
git status -sb
if git diff --cached --quiet; then
  echo "无新变更可提交"
else
  git commit -m "$(cat <<'EOF'
feat: 稳定板端 UI/按键与云端 Skill+LLM 判定

开机自启 jianwei、fb0 大字屏、按键不再同步阻塞 HTTP；
云端验真/守护走 skills/jianwei-antifraud + 聊天模型，并暴露 /api/ai-status。
EOF
)"
fi

echo "=== 3. 打包 zip/bundle ==="
mkdir -p "$SUB"
rm -f "$SUB/contest2026_159_weishizhixing-src.zip" "$SUB/contest2026_159_weishizhixing.bundle"
git bundle create "$SUB/contest2026_159_weishizhixing.bundle" --all
# zip without .git and object files
(
  cd "$(dirname "$DST")"
  zip -r "$SUB/contest2026_159_weishizhixing-src.zip" "$(basename "$DST")" \
    -x '*/.git/*' -x '*.o' -x '*/.venv/*' -x '*/__pycache__/*' -x '*/.env' -x '*/secrets.h'
)
ls -lh "$SUB"

echo "=== 4. 推送 GitHub ==="
if ! gh auth status >/dev/null 2>&1; then
  echo ""
  echo "尚未登录 GitHub。请在本终端执行："
  echo "  gh auth login"
  echo "登录成功后重新运行："
  echo "  bash $SRC/scripts/submit-contest.sh"
  echo ""
  echo "或把 zip 上传到你的 fork："
  echo "  $SUB/contest2026_159_weishizhixing-src.zip"
  exit 2
fi

# ensure fork remote
if ! git remote get-url origin >/dev/null 2>&1; then
  gh repo fork open-vela/contest2026_159_weishizhixing --remote --clone=false || true
  # if fork exists under user:
  USER=$(gh api user -q .login)
  git remote remove origin 2>/dev/null || true
  git remote add origin "https://github.com/${USER}/contest2026_159_weishizhixing.git"
fi

git push -u origin HEAD:dev-ai-contest-2026

# PR into official contest repo (team self-merge)
if gh pr list -R open-vela/contest2026_159_weishizhixing --head "$(gh api user -q .login):dev-ai-contest-2026" --json number -q '.[0].number' 2>/dev/null | grep -q '[0-9]'; then
  echo "PR 已存在"
  gh pr list -R open-vela/contest2026_159_weishizhixing --head "$(gh api user -q .login):dev-ai-contest-2026"
else
  gh pr create -R open-vela/contest2026_159_weishizhixing \
    --base dev-ai-contest-2026 \
    --head "$(gh api user -q .login):dev-ai-contest-2026" \
    --title "feat: 见微随身证 — openvela 板端 + Skill+LLM 云端判定" \
    --body "$(cat <<'EOF'
## Summary
- ESP32-S3-EYE openvela 应用 `jianwei`（BOOT 切验真/守护、fb0 屏、心跳、开机自启）
- 云端模式 A：OCR/ASR + `skills/jianwei-antifraud` Skill + 聊天模型风险判定
- 提交文档与一键编烧脚本

## Test plan
- [ ] `nsh>` 可见 / 自启后短按 BOOT 切 VERIFY/GUARD
- [ ] 看板 online + `/api/ai-status` 显示 skill+llm 配置
- [ ] 演示视频含 `nsh> jianwei`
EOF
)"
fi

echo "=== 完成 ==="
gh pr view -R open-vela/contest2026_159_weishizhixing --web 2>/dev/null || true
