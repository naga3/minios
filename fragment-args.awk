# kernel.fragment の各行を scripts/config の引数にする: NAME → -e NAME、!NAME → -d NAME、NAME=VALUE → --set-val NAME VALUE
NF {
    if ($1 ~ /^!/) printf "-d %s ", substr($1, 2)
    else if ($1 ~ /=/) { split($1, kv, "="); printf "--set-val %s %s ", kv[1], kv[2] }
    else printf "-e %s ", $1
}
END { print "" }
