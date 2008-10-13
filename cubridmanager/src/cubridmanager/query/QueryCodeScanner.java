package cubridmanager.query;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.text.TextAttribute;
import org.eclipse.jface.text.rules.IRule;
import org.eclipse.jface.text.rules.IToken;
import org.eclipse.jface.text.rules.IWhitespaceDetector;
import org.eclipse.jface.text.rules.MultiLineRule;
import org.eclipse.jface.text.rules.NumberRule;
import org.eclipse.jface.text.rules.RuleBasedScanner;
import org.eclipse.jface.text.rules.Token;
import org.eclipse.jface.text.rules.WhitespaceRule;
import org.eclipse.swt.SWT;

import cubridmanager.query.view.QueryEditor;

/**
 * This class scans through a code partition and colors it.
 */
public class QueryCodeScanner extends RuleBasedScanner {
	/**
	 * PerlCodeScanner constructor
	 */
	public QueryCodeScanner() {
		// Get the color manager
		ColorManager cm = QueryEditor.getApp().getColorManager();

		// Create the tokens for keywords, strings, and other (everything else)
		IToken keyword = new Token(new TextAttribute(cm
				.getColor(ColorManager.KEYWORD), cm
				.getColor(ColorManager.BACKGROUND), SWT.BOLD));
		IToken other = new Token(new TextAttribute(cm
				.getColor(ColorManager.DEFAULT)));
		IToken string = new Token(new TextAttribute(cm
				.getColor(ColorManager.STRING)));
		// IToken number = new Token(
		// new TextAttribute(cm.getColor(ColorManager.NUMBER)));
		// token for table : doesn't embodiment
		// TODO :using hyper link to execute schema navigator
		IToken table = new Token(new TextAttribute(cm
				.getColor(ColorManager.STRING)));
		// token for column : doesn't embodiment
		IToken column = new Token(new TextAttribute(cm
				.getColor(ColorManager.STRING)));

		// Use "other" for default
		setDefaultReturnToken(other);

		// Create the rules
		List rules = new ArrayList();

		// Add rules for strings
		rules.add(new MultiLineRule("'", "'", string, '\n'));
		rules.add(new MultiLineRule("\"", "\"", string, '\n'));
		// rules.add(new NumberRule(number));

		// Add rule for whitespace
		rules.add(new WhitespaceRule(new IWhitespaceDetector() {
			public boolean isWhitespace(char c) {
				return Character.isWhitespace(c);
			}
		}));

		// Add rule for keywords, and add the words to the rule
		// WordRule wordRule = new WordRule(new QueryWordDetector(), other);
		//
		// for (int i = 0, n = QuerySyntax.KEYWORDS.length; i < n; i++)
		// wordRule.addWord(QuerySyntax.KEYWORDS[i], keyword);
		// rules.add(wordRule);

		// Add word rule for keywords, types, and constants.

		UnsignedWordRule wordRule = new UnsignedWordRule(
				new QueryWordDetector(), other, // default token
				table, // table token
				column // column token
		);

		for (int i = 0; i < QuerySyntax.KEYWORDS.length; i++)
			wordRule.addWord(QuerySyntax.KEYWORDS[i], keyword);
		// rule for function : doesn't embodiment.
		// for (int i=0; i<QuerySyntax.FUNCTION.length; i++)
		// wordRule.addWord(QuerySyntax.FUNCTION[i], function);

		/* rule for table and column : doesn't embodiment */
		/*
		 * if(dictionary!=null){ Iterator it=dictionary.getTableNames();
		 * while(it.hasNext()){ wordRule.addWord(it.next().toString(), table); }
		 * it=dictionary.getCatalogSchemaNames(); while(it.hasNext()){
		 * wordRule.addWord(it.next().toString(), column); } }
		 */
		rules.add(wordRule);

		IRule[] result = new IRule[rules.size()];
		rules.toArray(result);
		setRules(result);
	}
}
