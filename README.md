# AudioHome
Projekt an der Hochschule Harz im Fach Ambient Assisted Living / Mobile Systeme

# Abstract
In Haushalten mit Massenmarktprodukten der Medientechnik hat jedes Gerät (Fernseher, Radio, Computer, etc.) seine eigenen Lautsprecher. Bei Zimmerlautstärke sind diese dementsprechend nur im selben Raum hörbar. Um dies zu umgehen, gibt es Systeme, die von verschiedenen Audioquellen genutzt werden können und den Ton im ganzen Haus verteilen. Diese sind unter der Bezeichnung „Multiroom“ bekannt und werden von Herstellern wie Teufel oder Sonos vertrieben. Die Steuerung erfolgt hierbei manuell über eine App und die Geräte kosten oft über hundert Euro pro Komponente. Mit einem solchen System ist es möglich, dass der Ton dem Nutzer durch die Wohnung folgt, wobei jedoch bei herkömmlichen Systemen manuelles Umschalten notwendig ist. Das kann dazu führen, dass Nutzer den Ton einfach lauter stellen um ihn über mehrere Räume hinweg zu hören. Eine daraus resultierende Lärmbelästigung kann innerhalb eines Mehrpersonenhaushaltes, aber auch innerhalb der Nachbarschaft zu Problemen führen. So fühlten sich beispielsweise etwa 20% der Deutschen bereits 2011 stark durch Lärm in der Nachbarschaft belästigt. 

Ziel des Projektes ist es, dieses Problem mit Hilfe einer automatischen Lokalisierung des Nutzers und entsprechender Anpassung der anzusteuernden Wiedergabegeräte zu minimieren. Die Kosten sollen dabei möglichst gering gehalten und gegebenenfalls bestehende Hardware des Nutzers integriert werden. Dafür ist es vorgesehen, dass alle Ein- und Ausgabegeräte zentral vernetzt werden.  

Es wird ein zentraler Server eingerichtet, welcher die Steuerung übernimmt. Die einzelnen Geräte werden dabei mittels Mikrocontroller mit dem Server vernetzt. Für die Lokalisierung werden raumbezogene Bluetooth Sender verwendet, die auf ein Gerät des Nutzers (Smartphone, Smartwatch) reagieren und so auf dessen Aufenthaltsraum innerhalb der Wohnung schließen lassen. Mit diesen Informationen kann die Audiowiedergabe entsprechend angepasst werden.

## Weitere Infos und Ergebnisse: 
[Projektbericht](https://github.com/PrinzDarknis/AudioHome/blob/master/Projektbericht%20(Git).pdf)
