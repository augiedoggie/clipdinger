#!/bin/env bash

usage() {
	echo "$0 [-s] [-f] [-c || -l || -L || <clip #>]"
	echo "	-c	Count clips"
	echo "	-l	List last 10 clips"
	echo "	-L	List all clips"
	echo "	-f	Use favorites instead of clips"
	echo "	-s	Print separator between clips"
	echo "	Only one of -c, -l, -L, or <clip #> can be used"
	exit 1
}

clipSig=application/x-vnd.humdinger-clipdinger

if [ $# -lt 1 ];then
	usage
	exit 1
fi

operation=0
clipTarget=Clips
useSeparator=false

checkProcess() {
	if [ -z "`roster -t | grep $clipSig`" ];then
		echo "Clipdinger not running!"
		exit 1
	fi
}

while getopts ":clLfs" option; do
	case "$option" in
		c)
			if [ $operation != 0 ];then
				usage
			fi
			operation=c
			;;
		l)
			if [ $operation != 0 ];then
				usage
			fi
			operation=l
			;;
		L)
			if [ $operation != 0 ];then
				usage
			fi
			operation=L
			;;
		f)
			clipTarget=Favorites
			;;
		s)
			useSeparator=true
			;;
		*)
			usage
			;;
	esac
done
shift $((OPTIND-1))

if [ $operation == 0 ] && [ -z "$1" ] ; then
	echo "Not enough arguments!"
	usage
fi

checkProcess

listClips() {
	for i in `seq $1 $2`;do
		if [ $useSeparator == true ];then
			echo "================Clip $i================"
		fi
		hey -o $clipSig get ${clipTarget/s/} $i
	done
}

case $operation in
	0)
		listClips $1 $1
		;;
	c)
		hey -o $clipSig count $clipTarget
		;;
	l)
		listClips 0 9
		;;
	L)
		((count = `hey -o $clipSig count $clipTarget` - 1))
		listClips 0 $count
		;;
esac
