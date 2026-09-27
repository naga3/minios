# カーネルに埋め込まれる「誰が・どこで・いつビルドしたか」を固定する（build.sh と build-efi.sh がリポジトリの中で読み込む）。
# 既定ではビルドした人のユーザー名・マシン名・日時が入り、公開するバイナリに個人の情報が残ってしまう
export KBUILD_BUILD_USER=minios
export KBUILD_BUILD_HOST=minios
export KBUILD_BUILD_VERSION=1
# 日時は最後のコミットの日時（UTC。タイムゾーンから地域が分からないように）。同じソースからは同じ文字列になる
KBUILD_BUILD_TIMESTAMP=$(TZ=UTC git log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M:%S UTC' 2>/dev/null || echo unknown)
export KBUILD_BUILD_TIMESTAMP
