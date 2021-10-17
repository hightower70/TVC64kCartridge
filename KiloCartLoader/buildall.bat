@sjasmplus.exe -Wno-rdlow --raw=kilocart_loader.bin --syntax=abf -DDECOMPRESSOR_ENABLED=0 kilocart.a80
@sjasmplus.exe -Wno-rdlow --raw=kilocart_decomp_loader.bin --syntax=abf -DDECOMPRESSOR_ENABLED=1 kilocart.a80
@bin2c -o kilocart_loader.c kilocart_loader.bin 
@bin2c -o kilocart_decomp_loader.c kilocart_decomp_loader.bin 
@copy kilocart_loader.c "../KiloCartImageBuilder/Source Files/kilocart_loader.c"
@copy kilocart_decomp_loader.c "../KiloCartImageBuilder/Source Files/kilocart_decomp_loader.c"