#!/bin/bash

#test ext2_cp

#set up test image and files

cp emptydisk.img ./test_emptydisk.img
echo -e "Testing cp with large file ./readimg_keegan and new name test_cp_readimg_keegan"
./ext2_cp test_emptydisk.img ./readimg_keegan /test_cp_readimg_keegan
blocks=`./readimg_keegan test_emptydisk.img  | grep "\[12\] Blocks:" | cut -d ' ' -f 3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20`
echo "Blocks: $blocks"
./print_block ./test_emptydisk.img 13376 $blocks > dd/readimg_keegan 
DIFF=$(diff dd/readimg_keegan ./readimg_keegan)
echo $DIFF
if [ "$DIFF" = "" ]; then
	echo -e "\n!!!Testing cp readimg_keegan: passed\n"
else 
	echo -e "\n!!!Testing cp readimg_keegan: failed\n"
fi

echo -e "Testing cp with small file dd/small_file with to /"
echo "test content" > dd/small_file
./ext2_cp test_emptydisk.img dd/small_file /
blocks=`./readimg_keegan test_emptydisk.img  | grep "\[13\] Blocks:" | cut -d ' ' -f 3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20`
echo "Blocks: $blocks"
./print_block ./test_emptydisk.img 13 $blocks > dd/test_small_file
diff dd/small_file dd/test_small_file
res=$?
DIFF=$(diff dd/small_file dd/test_small_file)
echo $DIFF
if [ "$DIFF" = "" ]; then
	echo -e "\n!!!Testing cp small_file: passed\n"
else 
	echo -e "\n!!!Testing cp small_file: failed\n"
fi


echo "Testing ln"
cp ./onedirectory.img ./test_onedirectory.img 
./ext2_ln test_onedirectory.img /hard_link /level1/bfile
./readimg_keegan test_onedirectory.img | grep -q "hard_link"
if [ $? -eq 0 ] ; then 
	echo -e "\n!!!hard link file name found in disk: passed\n"
else 
	echo -e "\n!!! hard link file not found in disk: failed \n"
fi
inode_for_hard_link=$(./readimg_keegan test_onedirectory.img | grep "hard_link" | cut -d " " -f 2)
inode_for_original_file=$(./readimg_keegan test_onedirectory.img | grep "bfile" | cut -d " " -f 2)
if [ $inode_for_original_file = $inode_for_hard_link ]; then
	echo -e "\n inodes are same: passed \n"
else 
	echo -e "\n inodes different: failed\n"
fi

echo "Testing soft link"
./ext2_ln test_onedirectory.img -s /level1/soft_link random_of_lengrandom_of_lengrandom_of_lengrandom_of_lengrandom_of_lengrandom_of_leng
./readimg_keegan test_onedirectory.img | grep -q "soft_link"
if [ $? -eq 0 ] ; then 
	echo -e "\n!!!soft link file name found in disk: passed\n"
else 
	echo -e "\n!!! soft link file not found in disk: failed \n"
fi
filetype=$(./readimg_keegan ./test_onedirectory.img  | grep "\[13\] type" | cut -d " " -f 3)
if [ $filetype = "l" ]; then
	echo -e "\n!!!soft link file type correct: passed\n"
else 
	echo -e "\n!!!soft link file type correct: failed\n"
	echo "Type $filetype"
fi
size=$(./readimg_keegan ./test_onedirectory.img  | grep "\[13\] type" | cut -d " " -f5)
if [ $size = "84" ]; then
	echo -e "\n!!!soft link size correct: passed\n"
else 
	echo -e "\n!!!soft link size correct: failed\n"
	echo "Size $size"
fi









