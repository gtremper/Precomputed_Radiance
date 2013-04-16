.SUFFIXES : .pdf .tex .zip

LATEX=pdflatex

FLAGS=-shell-escape

TEXFILES = $(wildcard *.tex ../*.tex)

PDF = hw3.pdf

pdf: $(PDF)

%.pdf: %.tex $(TEXFILES)
	pdflatex $(FLAGS) $*
	pdflatex $(FLAGS) $*
	rm -rf *~ *.log *.aux *.out *.bbl *.blg

clean:
	rm -rf *~ *.log hw2.pdf *.aux *.out *.bbl *.blg
