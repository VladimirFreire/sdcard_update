# ESP32ESP32 OTA (cartão SD)

Como funciona?

•	Basicamente, o OTA SD Card (SDMMC) obtém dois arquivos (.bin, .txt) em um caminho específico e grava na partição OTA.
•	O arquivo .bin é a imagem da partição e o .txt é o arquivo de configuração, com referência da versão. Em próximas atualizações será em arquivo json.
•	Entre no menuconfig e mude a configuração para uma partição de fábrica e duas ota, também altere a opção sem cryptografia.

Ligação

Esta biblioteca usa SDMMC_SLOT_1 com PULL-UPs internos. Você precisa colocar pull-ups externos.

•	CMD = GPIO_NUM_15.
•	CLK = GPIO_NUM_14.
•	D0 = GPIO_NUM_2.
•	CD = GPIO_NUM_0 , no botão (en) no DevKit, para simular o switch/SD 
•	WP = Não usado.
